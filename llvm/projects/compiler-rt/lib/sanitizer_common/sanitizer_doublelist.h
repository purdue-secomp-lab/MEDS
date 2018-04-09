//===-- sanitizer_doublelist.h ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//
//
// This file contains implementation of a list class to be used by
// Meds
//
//===---------------------------------------------------------------------===//

#ifndef SANITIZER_DOUBLELIST_H
#define SANITIZER_DOUBLELIST_H

#include "sanitizer_internal_defs.h"

namespace __sanitizer {

template<class Item>
struct DoubleList {
  void clear() {
    first_ = last_ = nullptr;
    size_ = 0;
  }

  bool empty() const { return size_ == 0; }
  uptr size() const { return size_; }

  void push_back(Item *x) {
    if (empty()) {
      x->next = nullptr;
      x->prev = nullptr;
      first_ = last_ = x;
      size_ = 1;
    } else {
      x->next = nullptr;
      x->prev = last_;
      last_->next = x;
      last_ = x;
      size_++;
    }
  }

  void push_front(Item *x) {
    if (empty()) {
      x->next = nullptr;
      first_ = last_ = x;
      size_ = 1;
    } else {
      x->next = first_;
      first_->prev = x;
      first_ = x;
      size_++;
    }
  }

  void pop_front() {
    CHECK(!empty());
    first_ = first_->next;
    if (first_)
      first_->prev = nullptr;
    if (!first_)
      last_ = nullptr;
    size_--;
  }

  Item *front() { return first_; }
  const Item *front() const { return first_; }
  Item *back() { return last_; }
  const Item *back() const { return last_; }

  void append_front(DoubleList<Item> *l) {
    CHECK_NE(this, l);
    if (l->empty())
      return;
    if (empty()) {
      *this = *l;
    } else if (!l->empty()) {
      l->last_->next = first_;
      first_->prev = l->last_;
      first_ = l->first_;
      size_ += l->size();
    }
    l->clear();
  }

  void remove(Item *x) {
    if (empty()) {
      return;
    }
    if (x->next && x->prev) {
      x->next->prev = x->prev;
      x->prev->next = x->next;
    } else if (x->next) {
      x->next->prev = nullptr;
      first_ = x->next;
    } else if (x->prev) {
      x->prev->next = nullptr;
      last_ = x->prev;
    } else {
      first_ = nullptr;
      last_ = nullptr;
    }
    x->next = nullptr;
    x->prev = nullptr;
    size_--;
  }

  void append_back(DoubleList<Item> *l) {
    CHECK_NE(this, l);
    if (l->empty())
      return;
    if (empty()) {
      *this = *l;
    } else {
      last_->next = l->first_;
      l->first_->prev = last_->next;
      last_ = l->last_;
      size_ += l->size();
    }
    l->clear();
  }

  void CheckConsistency() {
    if (size_ == 0) {
      CHECK_EQ(first_, 0);
      CHECK_EQ(last_, 0);
    } else {
      uptr count = 0;
      for (Item *i = first_; ; i = i->next) {
        count++;
        if (i == last_) break;
      }
      CHECK_EQ(size(), count);
      CHECK_EQ(last_->next, 0);
    }
  }

// private, don't use directly.
  uptr size_;
  Item *first_;
  Item *last_;
};

} // namespace __sanitizer

#endif // SANITIZER_DOUBLELIST_H

