"""
@file    OrderedMultiSet.py
@author  Jakob Erdmann
@author  Michael Behrisch
@date    2011-10-04
@version $Id$

multi set with insertion-order iteration
based on OrderedSet by Raymond Hettinger (c) , MIT-License
[http://code.activestate.com/recipes/576694/]

SUMO, Simulation of Urban MObility; see http://sumo.dlr.de/
Copyright (C) 2011-2015 DLR (http://www.dlr.de/) and contributors

This file is part of SUMO.
SUMO is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.
"""

import collections
KEY, PREV, NEXT = range(3)


class OrderedMultiSet(collections.MutableSet):

    def __init__(self, iterable=None):
        self.end = end = []
        # sentinel node for doubly linked list
        end += [None, end, end]
        # key --> [(key, prev1, next1), (key, prev2, next2), ...]
        self.map = collections.defaultdict(collections.deque)
        self.size = 0
        if iterable is not None:
            self |= iterable

    def __len__(self):
        return self.size

    def __contains__(self, key):
        return key in self.map

    def add(self, key):
        self.size += 1
        end = self.end
        curr = end[PREV]
        new = [key, curr, end]
        curr[NEXT] = end[PREV] = new
        self.map[key].append(new)

    def discard(self, key):
        if key in self.map:
            self.size -= 1
            deque = self.map[key]
            key, prev, next = deque.popleft()
            prev[NEXT] = next
            next[PREV] = prev
            if len(deque) == 0:
                self.map.pop(key)

    def __iter__(self):
        end = self.end
        curr = end[NEXT]
        while curr is not end:
            yield curr[KEY]
            curr = curr[NEXT]

    def __reversed__(self):
        end = self.end
        curr = end[PREV]
        while curr is not end:
            yield curr[KEY]
            curr = curr[PREV]

    def pop(self, last=True):
        if not self:
            raise KeyError('set is empty')
        key = next(reversed(self)) if last else next(iter(self))
        self.discard(key)
        return key

    def __repr__(self):
        if not self:
            return '%s()' % (self.__class__.__name__,)
        return '%s(%r)' % (self.__class__.__name__, list(self))

    def __eq__(self, other):
        if isinstance(other, self.__class__):
            return len(self) == len(other) and list(self) == list(other)
        return set(self) == set(other)

    def __del__(self):
        self.clear()                    # remove circular references

    def __sub__(self, other):
        result = self.__class__()
        for x in self:
            result.add(x)
        for x in other:
            result.discard(x)
        return result

    def __or__(self, other):
        result = self.__class__()
        for x in self:
            result.add(x)
        for x in other:
            result.add(x)
        return result
