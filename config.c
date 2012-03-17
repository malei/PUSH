/* config.c - Interpreter configuration
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


void push_config_set_interned(push_t *push, push_name_t key, push_val_t *val) {
  g_return_if_null(push);

  g_hash_table_insert(push->config, (void*)key, val);
}

void push_config_set(push_t *push, const char *key, push_val_t *val) {
  push_config_set_interned(push, push_intern_name(push, key), val);
}


push_val_t *push_config_get_interned(push_t *push, push_name_t key) {
  g_return_val_if_null(push, NULL);

  g_hash_table_lookup(push->config, (void*)key);
}

push_val_t *push_config_get(push_t *push, const char *key) {
  return push_config_get_interned(push, push_intern_name(push, key));
}
