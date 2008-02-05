/*
 * Copyright 2003-2007 Sun Microsystems, Inc.  All Rights Reserved.
 * Copyright 2007, 2008 Red Hat, Inc.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 */

#include "incls/_precompiled.incl"
#include "incls/_cppInterpreter_zero.cpp.incl"

#ifdef CC_INTERP

#define fixup_after_potential_safepoint()       \
  method = istate->method()

#define CALL_VM_NOCHECK(func)                   \
  thread->set_last_Java_frame();                \
  func;                                         \
  thread->reset_last_Java_frame();              \
  fixup_after_potential_safepoint()

void CppInterpreter::normal_entry(methodOop method, TRAPS)
{
  JavaThread *thread = (JavaThread *) THREAD;
  JavaStack *stack = thread->java_stack();

  // Adjust the caller's stack frame to accomodate any additional
  // local variables we have contiguously with our parameters.
  int extra_locals = method->max_locals() - method->size_of_parameters();
  if (extra_locals > 0) {
    if (extra_locals > stack->available_words()) {
      Unimplemented();
    }
    for (int i = 0; i < extra_locals; i++)
      stack->push(0);
  }

  // Allocate and initialize our frame.
  InterpreterFrame *frame = InterpreterFrame::build(stack, method, thread);
  thread->push_Java_frame(frame);
  interpreterState istate = frame->interpreter_state();

  intptr_t *result = NULL;
  int result_slots = 0;
  while (true) {
    // We can set up the frame anchor with everything we want at
    // this point as we are thread_in_Java and no safepoints can
    // occur until we go to vm mode.  We do have to clear flags
    // on return from vm but that is it.
    thread->set_last_Java_frame();

    // Call the interpreter
    if (JvmtiExport::can_post_interpreter_events())
      BytecodeInterpreter::runWithChecks(istate);
    else
      BytecodeInterpreter::run(istate);
    fixup_after_potential_safepoint();

    // Clear the frame anchor
    thread->reset_last_Java_frame();

    // Examine the message from the interpreter to decide what to do
    if (istate->msg() == BytecodeInterpreter::call_method) {
      method = istate->callee();

      // Trim back the stack to put the parameters at the top
      stack->set_sp(istate->stack() + 1);
    
      // Make the call
      address entry_point = istate->callee_entry_point();
      ((Interpreter::method_entry_t) entry_point) (method, THREAD);
      fixup_after_potential_safepoint();

      // Convert the result
      istate->set_stack(stack->sp() - 1);

      // Restore the stack
      stack->set_sp(istate->stack_limit() + 1);    

      // Resume the interpreter
      method = istate->method();
      istate->set_msg(BytecodeInterpreter::method_resume);
    }
    else if (istate->msg() == BytecodeInterpreter::more_monitors) {
      int monitor_words = frame::interpreter_frame_monitor_size();

      // Allocate the space
      if (monitor_words > stack->available_words()) {
        Unimplemented();
      }
      stack->alloc(monitor_words * wordSize);

      // Move the expression stack contents
      for (intptr_t *p = istate->stack() + 1; p < istate->stack_base(); p++)
        *(p - monitor_words) = *p;

      // Move the expression stack pointers
      istate->set_stack_limit(istate->stack_limit() - monitor_words);
      istate->set_stack(istate->stack() - monitor_words);
      istate->set_stack_base(istate->stack_base() - monitor_words);

      // Zero the new monitor so the interpreter can find it.
      ((BasicObjectLock *) istate->stack_base())->set_obj(NULL);

      // Resume the interpreter
      istate->set_msg(BytecodeInterpreter::got_monitors);
    }
    else if (istate->msg() == BytecodeInterpreter::return_from_method) {
      // Copy the result into the caller's frame
      result = istate->stack_base() - 1;
      result_slots = result - istate->stack();
      assert(result_slots >= 0 && result_slots <= 2, "what?");
      break;
    }
    else if (istate->msg() == BytecodeInterpreter::throwing_exception) {
      assert(HAS_PENDING_EXCEPTION, "should do");
      break;
    }
  }

  // Unwind the current frame
  thread->pop_Java_frame();

  // Pop our local variables
  stack->set_sp(stack->sp() + method->max_locals());

  // Push our result
  for (int i = 0; i < result_slots; i++)
    stack->push(result[-i]);
}

void CppInterpreter::native_entry(methodOop method, TRAPS)
{
  JavaThread *thread = (JavaThread *) THREAD;
  JavaStack *stack = thread->java_stack();

  // Allocate and initialize our frame
  InterpreterFrame *frame = InterpreterFrame::build(stack, method, thread);
  thread->push_Java_frame(frame);
  interpreterState istate = frame->interpreter_state();
  intptr_t *locals = istate->locals();

  // Make sure method is native and not abstract
  assert(method->is_native() && !method->is_abstract(), "should be");

  // Lock if necessary
  BasicObjectLock *monitor = NULL;
  if (method->is_synchronized()) {
    monitor = (BasicObjectLock*) istate->stack_base();
    oop lockee = monitor->obj();
    markOop disp = lockee->mark()->set_unlocked();

    monitor->lock()->set_displaced_header(disp);
    if (Atomic::cmpxchg_ptr(monitor, lockee->mark_addr(), disp) != disp) {
      if (thread->is_lock_owned((address) disp->clear_lock_bits())) {
        monitor->lock()->set_displaced_header(NULL);
      }
      else {
        CALL_VM_NOCHECK(InterpreterRuntime::monitorenter(thread, monitor));
        if (HAS_PENDING_EXCEPTION) {
          thread->pop_Java_frame();
          return;
        }
      }
    }
  }

  // Get the signature handler
  address handlerAddr = method->signature_handler();
  if (handlerAddr == NULL) {
    CALL_VM_NOCHECK(InterpreterRuntime::prepare_native_call(thread, method));
    if (HAS_PENDING_EXCEPTION) {
      thread->pop_Java_frame();
      return;
    }
    handlerAddr = method->signature_handler();
    assert(handlerAddr != NULL, "eh?");
  }
  if (handlerAddr == (address) InterpreterRuntime::slow_signature_handler) {
    CALL_VM_NOCHECK(handlerAddr =
      InterpreterRuntime::slow_signature_handler(thread, method, NULL, NULL));
    if (HAS_PENDING_EXCEPTION) {
      thread->pop_Java_frame();
      return;
    }
  }
  InterpreterRuntime::SignatureHandler *handler =
    InterpreterRuntime::SignatureHandler::from_handlerAddr(handlerAddr);

  // Get the native function entry point
  address function = method->native_function();
  assert(function != NULL, "should be set if signature handler is");

  // Build the argument list
  if (handler->argument_count() * 2 > stack->available_words()) {
    Unimplemented();
  }
  void **arguments =
    (void **) stack->alloc(handler->argument_count() * sizeof(void **));
  void **dst = arguments;

  void *env = thread->jni_environment();
  *(dst++) = &env;

  void *mirror = NULL;
  if (method->is_static()) {
    istate->set_oop_temp(
      method->constants()->pool_holder()->klass_part()->java_mirror());
    mirror = istate->oop_temp_addr();
    *(dst++) = &mirror;
  }

  intptr_t *src = locals;
  for (int i = dst - arguments; i < handler->argument_count(); i++) {
    ffi_type *type = handler->argument_type(i);
    if (type == &ffi_type_pointer) {
      if (*src) {
        stack->push((intptr_t) src);
        *(dst++) = stack->sp();
      }
      else {
        *(dst++) = src;
      }
      src--;
    }
    else if (type->size == 4) {
      *(dst++) = src--;
    }
    else if (type->size == 8) {
      src--;
      *(dst++) = src--;
    }
    else {
      ShouldNotReachHere();
    }
  }

  // Set up the Java frame anchor  
  thread->set_last_Java_frame();

  // Change the thread state to native
  ThreadStateTransition::transition_from_java(thread, _thread_in_native);

  // Make the call
  intptr_t result[4 - LogBytesPerWord];
  ffi_call(handler->cif(), (void (*)()) function, result, arguments);

  // Change the thread state back to Java
  ThreadStateTransition::transition_from_native(thread, _thread_in_Java);
  fixup_after_potential_safepoint();

  // Clear the frame anchor
  thread->reset_last_Java_frame();

  // If the result was an oop then unbox it and store it in
  // oop_temp where the garbage collector can see it before
  // we release the handle it might be protected by.
  if (handler->result_type() == &ffi_type_pointer) {
    if (result[0])
      istate->set_oop_temp(*(oop *) result[0]);
    else
      istate->set_oop_temp(NULL);
  }

  // Reset handle block
  thread->active_handles()->clear();
  
  // Unlock if necessary.  It seems totally wrong that this
  // is skipped in the event of an exception but apparently
  // the template interpreter does this so we do too.
  if (monitor && !HAS_PENDING_EXCEPTION) {
    BasicLock *lock = monitor->lock();
    markOop header = lock->displaced_header();
    oop rcvr = monitor->obj();
    monitor->set_obj(NULL);

    if (header != NULL) {
      if (Atomic::cmpxchg_ptr(header, rcvr->mark_addr(), lock) != lock) {
        monitor->set_obj(rcvr);
        {
          HandleMark hm(thread);
          CALL_VM_NOCHECK(InterpreterRuntime::monitorexit(thread, monitor));
        }
      }
    }
  }

  // Unwind the current activation
  thread->pop_Java_frame();

  // Pop our parameters
  stack->set_sp(stack->sp() + method->size_of_parameters());

  // Push our result
  if (!HAS_PENDING_EXCEPTION) {
    stack->set_sp(stack->sp() - type2size[method->result_type()]);

    switch (method->result_type()) {
    case T_VOID:
      break;

    case T_BOOLEAN:
#ifndef VM_LITTLE_ENDIAN
      result[0] <<= (BitsPerWord - BitsPerByte);
#endif
      SET_LOCALS_INT(*(jboolean *) result != 0, 0);
      break;

    case T_CHAR:
#ifndef VM_LITTLE_ENDIAN
      result[0] <<= (BitsPerWord - BitsPerShort);
#endif
      SET_LOCALS_INT(*(jchar *) result, 0);
      break;

    case T_BYTE:
#ifndef VM_LITTLE_ENDIAN
      result[0] <<= (BitsPerWord - BitsPerByte);
#endif
      SET_LOCALS_INT(*(jbyte *) result, 0);
      break;

    case T_SHORT:
#ifndef VM_LITTLE_ENDIAN
      result[0] <<= (BitsPerWord - BitsPerShort);
#endif
      SET_LOCALS_INT(*(jshort *) result, 0);
      break;

    case T_INT:
#ifndef VM_LITTLE_ENDIAN
      result[0] <<= (BitsPerWord - BitsPerInt);
#endif
      SET_LOCALS_INT(*(jint *) result, 0);
      break;

    case T_LONG:
      SET_LOCALS_LONG(*(jlong *) result, 0);
      break;
      
    case T_FLOAT:
      SET_LOCALS_FLOAT(*(jfloat *) result, 0);
      break;

    case T_DOUBLE:
      SET_LOCALS_DOUBLE(*(jdouble *) result, 0);
      break;

    case T_OBJECT:
    case T_ARRAY:
      SET_LOCALS_OBJECT(istate->oop_temp(), 0);
      break;

    default:
      ShouldNotReachHere();
    }
  }
}

void CppInterpreter::accessor_entry(methodOop method, TRAPS)
{
  JavaThread *thread = (JavaThread *) THREAD;
  JavaStack *stack = thread->java_stack();
  intptr_t *locals = stack->sp();

  // Drop into the slow path if we need a safepoint check
  if (SafepointSynchronize::do_call_back()) {
    normal_entry(method, THREAD);
    return;
  }

  // Load the object pointer and drop into the slow path
  // if we have a NullPointerException
  oop object = LOCALS_OBJECT(0);
  if (object == NULL) {
    normal_entry(method, THREAD);
    return;
  }

  // Read the field index from the bytecode, which looks like this:
  //  0:  aload_0
  //  1:  getfield
  //  2:    index
  //  3:    index
  //  4:  ireturn/areturn
  // NB this is not raw bytecode: index is in machine order
  u1 *code = method->code_base();
  assert(code[0] == Bytecodes::_aload_0 &&
         code[1] == Bytecodes::_getfield &&
         (code[4] == Bytecodes::_ireturn ||
          code[4] == Bytecodes::_areturn), "should do");
  u2 index = Bytes::get_native_u2(&code[2]);

  // Get the entry from the constant pool cache, and drop into
  // the slow path if it has not been resolved
  constantPoolCacheOop cache = method->constants()->cache();
  ConstantPoolCacheEntry* entry = cache->entry_at(index);
  if (!entry->is_resolved(Bytecodes::_getfield)) {
    normal_entry(method, THREAD);
    return;
  }

  // Get the result and push it onto the stack
  switch (entry->flag_state()) {
  case ltos:
  case dtos:
    if (stack->available_words() < 1) {
      Unimplemented();
    }
    stack->alloc(wordSize);
    break;
  }
  if (entry->is_volatile()) {
    switch (entry->flag_state()) {
    case ctos:
      SET_LOCALS_INT(object->char_field_acquire(entry->f2()), 0);
      break;

    case btos:
      SET_LOCALS_INT(object->byte_field_acquire(entry->f2()), 0);
      break;

    case stos:
      SET_LOCALS_INT(object->short_field_acquire(entry->f2()), 0);
      break;

    case itos:
      SET_LOCALS_INT(object->int_field_acquire(entry->f2()), 0);
      break;

    case ltos:
      SET_LOCALS_LONG(object->long_field_acquire(entry->f2()), 0);
      break;      
      
    case ftos:
      SET_LOCALS_FLOAT(object->float_field_acquire(entry->f2()), 0);
      break;

    case dtos:
      SET_LOCALS_DOUBLE(object->double_field_acquire(entry->f2()), 0);
      break;

    case atos:
      SET_LOCALS_OBJECT(object->obj_field_acquire(entry->f2()), 0);
      break;

    default:
      ShouldNotReachHere();
    }
  }
  else {
    switch (entry->flag_state()) {
    case ctos:
      SET_LOCALS_INT(object->char_field(entry->f2()), 0);
      break;

    case btos:
      SET_LOCALS_INT(object->byte_field(entry->f2()), 0);
      break;

    case stos:
      SET_LOCALS_INT(object->short_field(entry->f2()), 0);
      break;

    case itos:
      SET_LOCALS_INT(object->int_field(entry->f2()), 0);
      break;

    case ltos:
      SET_LOCALS_LONG(object->long_field(entry->f2()), 0);
      break;      
      
    case ftos:
      SET_LOCALS_FLOAT(object->float_field(entry->f2()), 0);
      break;

    case dtos:
      SET_LOCALS_DOUBLE(object->double_field(entry->f2()), 0);
      break;

    case atos:
      SET_LOCALS_OBJECT(object->obj_field(entry->f2()), 0);
      break;

    default:
      ShouldNotReachHere();
    }
  }
}

void CppInterpreter::empty_entry(methodOop method, TRAPS)
{
  JavaThread *thread = (JavaThread *) THREAD;
  JavaStack *stack = thread->java_stack();

  // Drop into the slow path if we need a safepoint check
  if (SafepointSynchronize::do_call_back()) {
    normal_entry(method, THREAD);
    return;
  }

  // Pop our parameters
  stack->set_sp(stack->sp() + method->size_of_parameters());
}

InterpreterFrame *InterpreterFrame::build(JavaStack*       stack,
                                          const methodOop  method,
                                          JavaThread*      thread)
{
  int monitor_words =
    method->is_synchronized() ? frame::interpreter_frame_monitor_size() : 0;
  int stack_words = method->is_native() ? 0 : method->max_stack();

  if (header_words + monitor_words + stack_words > stack->available_words()) {
    Unimplemented();
  }

  intptr_t *locals;
  if (method->is_native())
    locals = stack->sp() + (method->size_of_parameters() - 1);
  else
    locals = stack->sp() + (method->max_locals() - 1);

  stack->push(0); // next_frame, filled in later
  intptr_t *fp = stack->sp();
  assert(fp - stack->sp() == next_frame_off, "should be");

  stack->push(INTERPRETER_FRAME);
  assert(fp - stack->sp() == frame_type_off, "should be");

  interpreterState istate =
    (interpreterState) stack->alloc(sizeof(BytecodeInterpreter));
  assert(fp - stack->sp() == istate_off, "should be");

  istate->set_locals(locals);
  istate->set_method(method);
  istate->set_self_link(istate);
  istate->set_prev_link(NULL);
  istate->set_thread(thread);
  istate->set_bcp(method->is_native() ? NULL : method->code_base());
  istate->set_constants(method->constants()->cache());
  istate->set_msg(BytecodeInterpreter::method_entry);
  istate->set_oop_temp(NULL);
  istate->set_mdx(NULL);
  istate->set_callee(NULL);

  istate->set_monitor_base((BasicObjectLock *) stack->sp());
  if (method->is_synchronized()) {
    BasicObjectLock *monitor =
      (BasicObjectLock *) stack->alloc(monitor_words * wordSize);
    oop object;
    if (method->is_static())
      object = method->constants()->pool_holder()->klass_part()->java_mirror();
    else
      object = (oop) locals[0];
    monitor->set_obj(object);
  }

  istate->set_stack_base(stack->sp());
  istate->set_stack(stack->sp() - 1);
  if (stack_words)
    stack->alloc(stack_words * wordSize);
  istate->set_stack_limit(stack->sp() - 1);

  return (InterpreterFrame *) fp;
}

int AbstractInterpreter::BasicType_as_index(BasicType type)
{
  int i = 0;
  switch (type) {
    case T_BOOLEAN: i = 0; break;
    case T_CHAR   : i = 1; break;
    case T_BYTE   : i = 2; break;
    case T_SHORT  : i = 3; break;
    case T_INT    : i = 4; break;
    case T_LONG   : i = 5; break;
    case T_VOID   : i = 6; break;
    case T_FLOAT  : i = 7; break;
    case T_DOUBLE : i = 8; break;
    case T_OBJECT : i = 9; break;
    case T_ARRAY  : i = 9; break;
    default       : ShouldNotReachHere();
  }
  assert(0 <= i && i < AbstractInterpreter::number_of_result_handlers,
         "index out of bounds");
  return i;
}

address InterpreterGenerator::generate_empty_entry()
{
  if (!UseFastEmptyMethods)
    return NULL;

  return (address) CppInterpreter::empty_entry;
}

address InterpreterGenerator::generate_accessor_entry()
{
  if (!UseFastAccessorMethods)
    return NULL;

  return (address) CppInterpreter::accessor_entry;
}

address InterpreterGenerator::generate_native_entry(bool synchronized)
{
  return (address) CppInterpreter::native_entry;
}

address InterpreterGenerator::generate_normal_entry(bool synchronized)
{
  return (address) CppInterpreter::normal_entry;
}

address AbstractInterpreterGenerator::generate_method_entry(
    AbstractInterpreter::MethodKind kind) {

  address entry_point = NULL;
  bool synchronized = false;

  switch (kind) {
  case Interpreter::zerolocals:
    break;

  case Interpreter::zerolocals_synchronized:
    synchronized = true;
    break;

  case Interpreter::native:
    entry_point = ((InterpreterGenerator*)this)->generate_native_entry(false);
    break;

  case Interpreter::native_synchronized:
    entry_point = ((InterpreterGenerator*)this)->generate_native_entry(false);
    break;

  case Interpreter::empty:
    entry_point = ((InterpreterGenerator*)this)->generate_empty_entry();
    break;

  case Interpreter::accessor:
    entry_point = ((InterpreterGenerator*)this)->generate_accessor_entry();
    break;

  case Interpreter::abstract:
    entry_point = ((InterpreterGenerator*)this)->generate_abstract_entry();
    break;

  case Interpreter::java_lang_math_sin:
  case Interpreter::java_lang_math_cos:
  case Interpreter::java_lang_math_tan:
  case Interpreter::java_lang_math_abs:
  case Interpreter::java_lang_math_log:
  case Interpreter::java_lang_math_log10:
  case Interpreter::java_lang_math_sqrt:
    entry_point = ((InterpreterGenerator*)this)->generate_math_entry(kind);
    break;

  default:
    ShouldNotReachHere();
  }

  if (entry_point)
    return entry_point;

  return ((InterpreterGenerator*)this)->generate_normal_entry(false);
}

InterpreterGenerator::InterpreterGenerator(StubQueue* code)
 : CppInterpreterGenerator(code) {
   generate_all();
}

// Helper for (runtime) stack overflow checks

int AbstractInterpreter::size_top_interpreter_activation(methodOop method)
{
  return 0;
}

// Deoptimization helpers for C++ interpreter

int AbstractInterpreter::layout_activation(methodOop method,
                                           int tempcount,
                                           int popframe_extra_args,
                                           int moncount,
                                           int callee_param_count,
                                           int callee_locals,
                                           frame* caller,
                                           frame* interpreter_frame,
                                           bool is_top_frame)
{
  Unimplemented();
}

address CppInterpreter::return_entry(TosState state, int length)
{
  Unimplemented();
}

address CppInterpreter::deopt_entry(TosState state, int length)
{
  Unimplemented();
}

bool CppInterpreter::contains(address pc)
{
  ShouldNotCallThis();
}

// Result handlers and convertors

address CppInterpreterGenerator::generate_result_handler_for(
    BasicType type) {
  return ShouldNotReachHereStub();
}

address CppInterpreterGenerator::generate_tosca_to_stack_converter(
    BasicType type) {
  return ShouldNotReachHereStub();
}

address CppInterpreterGenerator::generate_stack_to_stack_converter(
    BasicType type) {
  return ShouldNotReachHereStub();
}

address CppInterpreterGenerator::generate_stack_to_native_abi_converter(
    BasicType type) {
  return ShouldNotReachHereStub();
}

#endif // CC_INTERP