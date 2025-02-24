/**
 * coroutine context fiber container
 */
#ifndef COPP_COROUTINE_CONTEXT_COROUTINE_CONTEXT_FIBER_CONTAINER_H
#define COPP_COROUTINE_CONTEXT_COROUTINE_CONTEXT_FIBER_CONTAINER_H

#pragma once

#include <cstddef>

#include <libcopp/coroutine/coroutine_context_fiber.h>
#include <libcopp/stack/stack_allocator.h>
#include <libcopp/stack/stack_traits.h>
#include <libcopp/utils/errno.h>

#if defined(LIBCOPP_MACRO_ENABLE_WIN_FIBER) && LIBCOPP_MACRO_ENABLE_WIN_FIBER
namespace copp {
/**
 * @brief coroutine container
 * contain stack context, stack allocator and runtime fcontext
 */
template <typename TALLOC>
class coroutine_context_fiber_container : public coroutine_context_fiber {
 public:
  using coroutine_context_type = coroutine_context_fiber;
  using base_type = coroutine_context_fiber;
  using allocator_type = TALLOC;
  using this_type = coroutine_context_fiber_container<allocator_type>;
  using ptr_t = libcopp::util::intrusive_ptr<this_type>;
  using callback_t = coroutine_context_fiber::callback_t;

  COROUTINE_CONTEXT_BASE_USING_BASE(base_type)

 private:
  coroutine_context_fiber_container(const allocator_type &alloc) LIBCOPP_MACRO_NOEXCEPT : alloc_(alloc),
                                                                                          ref_count_(0) {}

  coroutine_context_fiber_container(allocator_type &&alloc) LIBCOPP_MACRO_NOEXCEPT : alloc_(std::move(alloc)),
                                                                                     ref_count_(0) {}

 public:
  ~coroutine_context_fiber_container() {}

  /**
   * @brief get stack allocator
   * @return stack allocator
   */
  inline const allocator_type &get_allocator() const LIBCOPP_MACRO_NOEXCEPT { return alloc_; }

  /**
   * @brief get stack allocator
   * @return stack allocator
   */
  inline allocator_type &get_allocator() LIBCOPP_MACRO_NOEXCEPT { return alloc_; }

 public:
  /**
   * @brief create and init coroutine with specify runner and specify stack size
   * @param runner runner
   * @param stack_sz stack size
   * @param private_buffer_size private buffer size
   * @param coroutine_size extend buffer before coroutine
   * @return COPP_EC_SUCCESS or error code
   */
  static ptr_t create(callback_t &&runner, allocator_type &alloc, size_t stack_sz = 0, size_t private_buffer_size = 0,
                      size_t coroutine_size = 0) LIBCOPP_MACRO_NOEXCEPT {
    ptr_t ret;
    if (0 == stack_sz) {
      stack_sz = stack_traits::default_size();
    }

    // padding to sizeof size_t
    coroutine_size = align_address_size(coroutine_size);
    const size_t this_align_size = align_address_size(sizeof(this_type));
    coroutine_size += this_align_size;
    private_buffer_size = coroutine_context_fiber::align_private_data_size(private_buffer_size);

    // stack allocator is just used for allocate coroutine and private data
    stack_context callee_stack;
    alloc.allocate(callee_stack, coroutine_size + private_buffer_size);

    if (nullptr == callee_stack.sp) {
      return ret;
    }

    // placement new
    unsigned char *this_addr = reinterpret_cast<unsigned char *>(callee_stack.sp);
    // stack down
    this_addr -= private_buffer_size + this_align_size;
    ret.reset(new (reinterpret_cast<void *>(this_addr)) this_type(std::move(alloc)));

    // callee_stack and alloc unavailable any more.
    if (ret) {
      ret->callee_stack_ = std::move(callee_stack);
    } else {
      alloc.deallocate(callee_stack);
      return ret;
    }

    // after this call runner will be unavailable
    // stack_sz is used for stack reserve size of fiber
    if (coroutine_context_fiber::create(ret.get(), std::move(runner), ret->callee_stack_, coroutine_size,
                                        private_buffer_size, stack_sz) < 0) {
      ret.reset();
    }

    return ret;
  }

  template <class TRunner>
  static inline ptr_t create(TRunner *runner, allocator_type &alloc, size_t stack_size = 0,
                             size_t private_buffer_size = 0, size_t coroutine_size = 0) LIBCOPP_MACRO_NOEXCEPT {
    if (nullptr == runner) {
      return create(callback_t(), alloc, stack_size, private_buffer_size, coroutine_size);
    }

    return create([runner](void *private_data) { return (*runner)(private_data); }, alloc, stack_size,
                  private_buffer_size, coroutine_size);
  }

  static inline ptr_t create(int (*fn)(void *), allocator_type &alloc, size_t stack_size = 0,
                             size_t private_buffer_size = 0, size_t coroutine_size = 0) LIBCOPP_MACRO_NOEXCEPT {
    if (nullptr == fn) {
      return create(callback_t(), alloc, stack_size, private_buffer_size, coroutine_size);
    }

    return create(callback_t(fn), alloc, stack_size, private_buffer_size, coroutine_size);
  }

  static ptr_t create(callback_t &&runner, size_t stack_size = 0, size_t private_buffer_size = 0,
                      size_t coroutine_size = 0) LIBCOPP_MACRO_NOEXCEPT {
    allocator_type alloc;
    return create(std::move(runner), alloc, stack_size, private_buffer_size, coroutine_size);
  }

  template <class TRunner>
  static inline ptr_t create(TRunner *runner, size_t stack_size = 0, size_t private_buffer_size = 0,
                             size_t coroutine_size = 0) LIBCOPP_MACRO_NOEXCEPT {
    return create([runner](void *private_data) { return (*runner)(private_data); }, stack_size, private_buffer_size,
                  coroutine_size);
  }

  static inline ptr_t create(int (*fn)(void *), size_t stack_size = 0, size_t private_buffer_size = 0,
                             size_t coroutine_size = 0) LIBCOPP_MACRO_NOEXCEPT {
    return create(callback_t(fn), stack_size, private_buffer_size, coroutine_size);
  }

  inline size_t use_count() const LIBCOPP_MACRO_NOEXCEPT { return ref_count_.load(); }

 private:
  coroutine_context_fiber_container(const coroutine_context_fiber_container &) = delete;

 private:
  friend void intrusive_ptr_add_ref(this_type *p) {
    if (p == nullptr) {
      return;
    }

    ++p->ref_count_;
  }

  friend void intrusive_ptr_release(this_type *p) {
    if (p == nullptr) {
      return;
    }

    size_t left = --p->ref_count_;
    if (0 == left) {
      allocator_type copy_alloc(std::move(p->alloc_));
      stack_context copy_stack(std::move(p->callee_stack_));

      // then destruct object and reset data
      p->~coroutine_context_fiber_container();

      // final, recycle stack buffer
      copy_alloc.deallocate(copy_stack);
    }
  }

 private:
  allocator_type alloc_; /** stack allocator **/
#  if defined(LIBCOPP_DISABLE_ATOMIC_LOCK) && LIBCOPP_DISABLE_ATOMIC_LOCK
  libcopp::util::lock::atomic_int_type<libcopp::util::lock::unsafe_int_type<size_t> > ref_count_; /** status **/
#  else
  libcopp::util::lock::atomic_int_type<size_t> ref_count_; /** status **/
#  endif
};

using coroutine_fiber_context_default = coroutine_context_fiber_container<allocator::stack_allocator_malloc>;
}  // namespace copp

#endif

#endif
