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

extern void monitorInit(Monitor *mon);
extern void monitorLock(Monitor *mon, Thread *self);
extern void monitorUnlock(Monitor *mon, Thread *self);
extern int monitorWait(Monitor *mon, Thread *self, long long ms, int ns);
extern int monitorNotify(Monitor *mon, Thread *self);
extern int monitorNotifyAll(Monitor *mon, Thread *self);

extern void objectLock(Object *ob);
extern void objectUnlock(Object *ob);
extern void objectNotify(Object *ob);
extern void objectNotifyAll(Object *ob);
extern void objectWait(Object *ob, long long ms, int ns);
