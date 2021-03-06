/* interpreter.c - The PUSH interpreter
 *
 * Copyright (c) 2012 Janosch Gräf <janosch.graef@gmx.net>
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <glib.h>

#include "push.h"



push_t *push_new_full(push_bool_t default_instructions, push_bool_t default_config, push_gc_t *gc, push_interrupt_handler_t interrupt_handler, push_step_hook_t step_hook) {
  push_t *push;

  push = g_slice_new(push_t);

  /* create mutex & lock it */
  g_static_mutex_init(&push->mutex);
  g_static_mutex_lock(&push->mutex);

  push->interrupt_handler = interrupt_handler;
  push->step_hook = step_hook;
  push->rand = g_rand_new();
  push->names = g_string_chunk_new(PUSH_NAME_STORAGE_BLOCK_SIZE);
  push->gc = gc == NULL ? push_gc_global() : gc;

  /* create hash tables */
  push->config = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  push->bindings = g_hash_table_new(NULL, NULL);
  push->instructions = g_hash_table_new_full(NULL, NULL, NULL, (GDestroyNotify)push_instr_destroy);

  /* initialize stacks */
  push->boolean = push_stack_new();
  push->code = push_stack_new();
  push->exec = push_stack_new();
  push->integer = push_stack_new();
  push->name = push_stack_new();
  push->real = push_stack_new();

  /* add interpreter to garbage collector */
  push_gc_add_interpreter(push->gc, push);

  if (default_config) {
    /* default configuration */
    push_config_set(push, "MIN-RANDOM-INT", push_val_new(push, PUSH_TYPE_INT, -100));
    push_config_set(push, "MAX-RANDOM-INT", push_val_new(push, PUSH_TYPE_INT, 100));
    push_config_set(push, "MIN-RANDOM-REAL", push_val_new(push, PUSH_TYPE_REAL, 0.0));
    push_config_set(push, "MAX-RANDOM-REAL", push_val_new(push, PUSH_TYPE_REAL, 1.0));
    push_config_set(push, "MIN-RANDOM-NAME-LENGTH", push_val_new(push, PUSH_TYPE_INT, 2));
    push_config_set(push, "MAX-RANDOM-NAME-LENGTH", push_val_new(push, PUSH_TYPE_INT, 16));
    push_config_set(push, "MAX-POINTS-IN-RANDOM-EXPRESSIONS", push_val_new(push, PUSH_TYPE_INT, 100));
    push_config_set(push, "NEW-ERC-NAME-PROBABILITY", push_val_new(push, PUSH_TYPE_REAL, 0.01));
  }

  if (default_instructions) {
    /* load default instruction set
     * TODO: take from config, which instructions to load
     */
    push_add_dis(push);
  }

  g_static_mutex_unlock(&push->mutex);

  return push;
}


push_t *push_new(void) {
  return push_new_full(TRUE, TRUE, NULL, NULL, NULL);
}


void push_destroy(push_t *push) {
  g_return_if_null(push);

  /* unlink from GC & lock */
  push_gc_remove_interpreter(push->gc, push);
  g_static_mutex_lock(&push->mutex);

  /* destroy stacks */
  push_stack_destroy(push->boolean);
  push_stack_destroy(push->code);
  push_stack_destroy(push->exec);
  push_stack_destroy(push->integer);
  push_stack_destroy(push->name);
  push_stack_destroy(push->real);

  /* destroy hash tables */
  g_hash_table_destroy(push->bindings);
  g_hash_table_destroy(push->config);
  g_hash_table_destroy(push->instructions);

  /* destroy random number generator */
  g_rand_free(push->rand);

  /* destroy storage for interned strings */
  g_string_chunk_free(push->names);

  /* destroy execution mutex */
  g_static_mutex_free(&push->mutex);

  g_slice_free(push_t, push);
}


push_t *push_copy(push_t *push) {
  // NOTE: deprecated, also not thread-safe!
  push_t *new_push;
  GHashTableIter iter;
  const char *key;
  push_val_t *val;
  push_instr_t *instr;

  new_push = push_new_full(FALSE, FALSE, push->gc, push->interrupt_handler, push->step_hook);

  /* copy configuration */
  g_hash_table_iter_init(&iter, push->config);
  while (g_hash_table_iter_next(&iter, (void*)&key, (void*)&val)) {
    push_config_set(new_push, key, push_val_copy(val, new_push));
  }

  /* copy bindings */
  g_hash_table_iter_init(&iter, push->bindings);
  while (g_hash_table_iter_next(&iter, (void*)&key, (void*)&val)) {
    push_define(new_push, push_intern_name(new_push, key), push_val_copy(val, new_push));
  }

  /* copy instructions */
  g_hash_table_iter_init(&iter, push->instructions);
  while (g_hash_table_iter_next(&iter, (void*)&key, (void*)&instr)) {
    push_instr_reg(new_push, key, instr->func, instr->userdata);
  }

  /* copy stacks */
  new_push->boolean = push_stack_copy(push->boolean, new_push);
  new_push->code = push_stack_copy(push->code, new_push);
  new_push->exec = push_stack_copy(push->exec, new_push);
  new_push->integer = push_stack_copy(push->integer, new_push);
  new_push->name = push_stack_copy(push->name, new_push);
  new_push->real = push_stack_copy(push->real, new_push);

  return new_push;
}


void push_flush(push_t *push) {
  /* flush all stacks */
  push_stack_flush(push->boolean);
  push_stack_flush(push->code);
  push_stack_flush(push->exec);
  push_stack_flush(push->integer);
  push_stack_flush(push->name);
  push_stack_flush(push->real);

  /* remove all bindings */
  g_hash_table_remove_all(push->bindings);
}


push_name_t push_intern_name(push_t *push, const char *name) {
  g_return_val_if_null(push, NULL);

  return g_string_chunk_insert_const(push->names, name);
}


void push_config_set(push_t *push, const char *key, push_val_t *val) {
  g_hash_table_insert(push->config, g_strdup(key), val);
}


push_val_t *push_config_get(push_t *push, const char *key) {
  return g_hash_table_lookup(push->config, key);
}


void push_define(push_t *push, push_name_t name, push_val_t *val) {
  g_return_if_null(push);

  if (!push_check_name(val) && !push_check_name(val)) {
    g_hash_table_insert(push->bindings, name, val);
  }
}


void push_undef(push_t *push, push_name_t name) {
  push_val_t *val;

  g_return_if_null(push);

  val = g_hash_table_lookup(push->bindings, name);
  if (val != NULL) {
    g_hash_table_remove(push->bindings, name);
  }
}


push_val_t *push_lookup(push_t *push, push_name_t name) {
  g_hash_table_lookup(push->bindings, name);
}


void push_do_val(push_t *push, push_val_t *val) {
  push_val_t *val2;

  g_return_if_null(push);

  switch (val->type) {
    case PUSH_TYPE_BOOL:
      push_stack_push(push->boolean, val);
      break;

    case PUSH_TYPE_CODE:
      g_return_if_null(val);

      /* push all values of code object onto exec stack in reverse order */
      push_code_push_elements(val->code, push->exec);
      break;

    case PUSH_TYPE_INT:
      push_stack_push(push->integer, val);
      break;

    case PUSH_TYPE_INSTR:
      g_return_if_null(val);

      push_call_instr(push, val->instr);
      break;

    case PUSH_TYPE_NAME:
      g_return_if_null(val);

      val2 = push_lookup(push, val->name);
      if (val2 != NULL) {
        push_stack_push(push->exec, val2);
      }
      else {
        push_stack_push(push->name, val);
      }
      break;

    case PUSH_TYPE_REAL:
      push_stack_push(push->real, val);
      break;

    default:
      g_warning("Unknown value type: %d", val->type);
      break;
  }
}


/* Do one single step
 * NOTE: Doesn't clear the interrupt flag
 * NOTE: Doesn't check execution mutex
 * NOTE: Doesn't call the garbage collector
 */
push_bool_t push_step(push_t *push) {
  push_val_t *val;

  val = push_stack_pop(push->exec);
  if (val != NULL) {
    push_do_val(push, val);
  }

  if (push->interrupt_flag != 0) {
    /* call the interrupt handler */
    if (push->interrupt_handler != NULL && push->interrupt_flag > 0) {
      push->interrupt_handler(push, push->interrupt_flag, push->userdata);
    }
    return FALSE;
  }

  if (push->step_hook != NULL) {
    /* call the step hook */
    if (!push->step_hook(push, push->userdata) ){
      return FALSE;
    }
  }

  return val != NULL;
}


push_int_t push_run(push_t *push, push_int_t max_steps) {
  push_int_t i;

  g_return_if_null(push);

  g_static_mutex_lock(&push->mutex);

  /* clear interrupt flag */
  push->interrupt_flag = 0;

  /* run until max_steps reached, EXEC stack is empty or an interrupt was raised */
  if (max_steps > 0) {
    for (i = 0; i < max_steps && push_step(push); i++);
  }
  else {
    for (i = 0; push_step(push); i++);
  }

  g_static_mutex_unlock(&push->mutex);

  return i;
}


push_bool_t push_done(push_t *push) {
  g_return_if_null(push);

  return push_stack_is_empty(push->exec);
}


void push_interrupt(push_t *push, push_int_t interrupt_flag) {
  push->interrupt_flag = interrupt_flag;
}


/* NOTE: free returned string with g_free or push_free */
char *push_dump_state(push_t *push) {
  GString *xml;

  xml = g_string_new("<?xml version=\"1.0\" ?>\n");

  g_static_mutex_lock(&push->mutex);
  push_serialize(xml, 0, push);
  g_static_mutex_unlock(&push->mutex);

  return g_string_free(xml, 0);
}
void push_free(void *ptr) {
  g_free(ptr);
}

void push_load_state(push_t *push, const char *xml) {
  g_static_mutex_lock(&push->mutex);
  push_unserialize_parse(push, xml, NULL);
  g_static_mutex_unlock(&push->mutex);
}

