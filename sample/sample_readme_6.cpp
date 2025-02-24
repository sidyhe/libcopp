﻿/*
 * sample_readme_6.cpp
 *
 *  Created on: 2020-05-20
 *      Author: owent
 *
 *  Released under the MIT license
 */

#include <assert.h>
#include <iostream>
#include <list>

// include manager header file
#include <libcopp/future/context.h>
#include <libcopp/future/future.h>

struct example_result_message_t {
  int return_code;
  int response_code;
};
struct example_poller;

typedef copp::future::result_type<example_result_message_t,  // polled data if success
                                  int                        // error code if failed
                                  >
    example_result_t;
typedef copp::future::future_with_waker<example_result_t> example_future_t;
typedef copp::future::context<example_poller> example_context_t;

std::list<example_context_t*> g_executor;

struct example_poller {
  example_result_t::storage_type* result;
  std::list<example_context_t*>::iterator iter;
  example_poller() : result(nullptr) { iter = g_executor.end(); }
  ~example_poller() {
    // cleanup and remove from executor
    if (iter != g_executor.end()) {
      g_executor.erase(iter);
    }
  }
  void operator()(example_future_t& future, example_context_t& ctx) {
    if (nullptr != result) {
      future.poll_data() = *result;
      result = nullptr;
      // remove from executor
      if (iter != g_executor.end()) {
        g_executor.erase(iter);
        iter = g_executor.end();
      }
    } else {
      // add to executor
      if (iter == g_executor.end()) {
        iter = g_executor.insert(g_executor.end(), &ctx);
      }
    }
  }
};

void run_with_custom_context() {
  example_future_t future;
  example_context_t context;

  assert(future.is_ready() == false);

  // poll context for the first time and setup waker
  // future should only poll one context
  future.poll(context);

  while (!g_executor.empty()) {
    example_result_message_t msg;
    // set a result message
    msg.return_code = 0;
    msg.response_code = 200;
    // if both success type and error type is a small trivial type, storage_type will be result_type with union of
    // success type and error type else storage_type will be std::unique_ptr<result_type> result_type::make_success(...)
    // and result_type::make_error(...) will make sure to use the correct storage type
    example_result_t::storage_type result_storage = example_result_t::make_success(msg);
    (*g_executor.begin())->get_private_data().result = &result_storage;
    // just call context::wake to wakeup and poll again
    (*g_executor.begin())->wake();
  }

  // Then future is ready
  assert(future.is_ready() == true);
  example_result_t* result = future.data();
  assert(result != nullptr);

  assert(result->is_success());
  assert(200 == result->get_success()->response_code);
  assert(false == result->is_error());
  assert(nullptr == result->get_error());

  std::cout << "Got future success response code: " << result->get_success()->response_code << std::endl;
}

static void custom_poller_function(copp::future::context<void>&,
                                   copp::future::context<void>::poll_event_data evt_data) {
  if (nullptr == evt_data.private_data) {
    return;
  }

  example_future_t* future = reinterpret_cast<example_future_t*>(evt_data.future_ptr);

  example_result_message_t* msg = reinterpret_cast<example_result_message_t*>(evt_data.private_data);

  // if both success type and error type is a small trivial type, storage_type will be result_type with union of success
  // type and error type else storage_type will be std::unique_ptr<result_type> result_type::make_success(...) and
  // result_type::make_error(...) will make sure to use the correct storage type
  future->poll_data() = example_result_t::make_success(*msg);
}

void run_with_void_context() {
  example_future_t future;
  copp::future::context<void> context(copp::future::context<void>::construct(custom_poller_function));
  // upper code equal to:
  // copp::future::context<void> context;
  // context.set_poll_fn(custom_poller_function);

  assert(future.is_ready() == false);

  // poll context for the first time and setup waker
  // future should only poll one context
  future.poll(context);

  while (!future.is_ready()) {
    example_result_message_t msg;
    // set a result message
    msg.return_code = 0;
    msg.response_code = 200;
    context.set_private_data(reinterpret_cast<void*>(&msg));

    context.wake();
  }

  // Then future is ready
  assert(future.is_ready() == true);
  example_result_t* result = future.data();
  assert(result != nullptr);

  assert(result->is_success());
  assert(200 == result->get_success()->response_code);
  assert(false == result->is_error());
  assert(nullptr == result->get_error());

  std::cout << "Got future success response code(context<void>): " << result->get_success()->response_code << std::endl;
}

int main() {
  run_with_custom_context();
  run_with_void_context();
  return 0;
}
