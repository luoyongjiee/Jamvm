/*
 * Copyright (C) 2003 Robert Lougher <rob@lougher.demon.co.uk>.
 *
 * This file is part of JamVM.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include "jam.h"
#include "hash.h"

void resizeHash(HashTable *table, int new_size) {
    HashEntry *new_table = (HashEntry*)malloc(sizeof(HashEntry)*new_size);
    int i;

    memset(new_table, 0, sizeof(HashEntry)*new_size);

    for(i = table->hash_size-1; i >= 0; i--) {
        void *ptr = table->hash_table[i].data;
        if(ptr != NULL) {
            int hash = table->hash_table[i].hash;
            int new_index = hash & (new_size - 1);

            while(new_table[new_index].data != NULL)
                new_index = (new_index+1) & (new_size - 1);

            new_table[new_index].hash = hash;
            new_table[new_index].data = ptr;
        }
    }

    free(table->hash_table);
    table->hash_table = new_table;
    table->hash_size = new_size;
}
