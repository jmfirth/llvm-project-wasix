// RUN: split-file %s %t
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/positive.c | FileCheck %s --check-prefix=P
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/positive.c | FileCheck %s --check-prefix=P
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/negative.c | FileCheck %s --check-prefix=N --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/negative.c | FileCheck %s --check-prefix=N --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/asm.c | FileCheck %s --check-prefix=ASM --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/positive.c | FileCheck %s --check-prefix=NATIVE --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=c23 -emit-llvm -o - %t/prototyped.c | FileCheck %s --check-prefix=PROTO --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=c++17 -x c++ -emit-llvm -o - %t/prototyped.c | FileCheck %s --check-prefix=PROTO --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu89 -Wno-implicit-int -Wno-error=incompatible-function-pointer-types -emit-llvm -disable-llvm-passes -o - %t/coverage.c | FileCheck %s --check-prefix=COVER --implicit-check-not=onintr.wasm.callback
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu89 -Wno-implicit-int -Wno-error=incompatible-function-pointer-types -emit-llvm -disable-llvm-passes -o - %t/coverage.c | FileCheck %s --check-prefix=COVER --implicit-check-not=onintr.wasm.callback
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/weakref.c | FileCheck %s --check-prefix=WEAK --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/weakref.c | FileCheck %s --check-prefix=WEAK --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/asmlabel.c | FileCheck %s --check-prefix=LABEL --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/asmlabel.c | FileCheck %s --check-prefix=LABEL --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -DPRAGMA -emit-llvm -disable-llvm-passes -o - %t/asmlabel.c | FileCheck %s --check-prefix=LABEL --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu99 -DPRAGMA -emit-llvm -disable-llvm-passes -o - %t/asmlabel.c | FileCheck %s --check-prefix=LABEL --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -fregister-global-dtors-with-atexit -fno-use-cxa-atexit -emit-llvm -disable-llvm-passes -o - %t/dtor.c | FileCheck %s --check-prefix=DTOR --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu99 -fregister-global-dtors-with-atexit -fno-use-cxa-atexit -emit-llvm -disable-llvm-passes -o - %t/dtor.c | FileCheck %s --check-prefix=DTOR --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/alias.c | FileCheck %s --check-prefix=ALIAS --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/alias.c | FileCheck %s --check-prefix=ALIAS --implicit-check-not=.wasm.callback

// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/rejected.c | FileCheck %s --check-prefix=REJECTED --implicit-check-not=.wasm.callback --implicit-check-not="internal alias"
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/rejected.c | FileCheck %s --check-prefix=REJECTED --implicit-check-not=.wasm.callback --implicit-check-not="internal alias"
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -O2 -emit-llvm -o - %t/positive.c | FileCheck %s --check-prefix=OPT
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu99 -O2 -emit-llvm -o - %t/positive.c | FileCheck %s --check-prefix=OPT
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -x objective-c -fobjc-runtime=gnustep-1.8 -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/positive.c | FileCheck %s --check-prefix=NATIVE --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -x objective-c -fobjc-runtime=gnustep-1.8 -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/positive.c | FileCheck %s --check-prefix=NATIVE --implicit-check-not=.wasm.callback
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/section.c | FileCheck %s --check-prefix=SECTION
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/section.c | FileCheck %s --check-prefix=SECTION
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -verify -emit-llvm -disable-llvm-passes -o - %t/diagnostics.c | FileCheck %s --check-prefix=DIAG
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu99 -verify -emit-llvm -disable-llvm-passes -o - %t/diagnostics.c | FileCheck %s --check-prefix=DIAG
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -Werror -DAUDIT_ERROR -emit-llvm -disable-llvm-passes -o - %t/diagnostics.c | FileCheck %s --check-prefix=AUDIT-ERROR --implicit-check-not=.wasm.callback --implicit-check-not="internal alias"
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu99 -Werror -DAUDIT_ERROR -emit-llvm -disable-llvm-passes -o - %t/diagnostics.c | FileCheck %s --check-prefix=AUDIT-ERROR --implicit-check-not=.wasm.callback --implicit-check-not="internal alias"

// All address expressions, including initializers, address-of, casts, and
// comparisons, must refer to ONE canonical symbol. No API spelling selects it.
// P-DAG: @saved = {{.*}}global ptr @thread.wasm.callback
// P-DAG: @addresses = {{.*}}global [2 x ptr] [ptr @thread.wasm.callback, ptr @thread.wasm.callback]
// P-DAG: @pair = {{.*}}global %struct.pair { ptr @thread.wasm.callback }
// P-DAG: ret ptr @thread.wasm.callback
// P-DAG: call void @take_start(ptr {{.*}}@thread.wasm.callback)
// P-DAG: call void @take_signal(ptr {{.*}}@handler.wasm.callback)
// P-DAG: call void @take_cleanup(ptr {{.*}}@cleanup.wasm.callback)
// P-DAG: call void @take_compare(ptr {{.*}}@compare.wasm.callback)
// P-DAG: icmp eq ptr {{.*}}, @thread.wasm.callback
// P-DAG: call {{.*}}ptr @thread()
// P-DAG: define internal {{.*}}ptr @thread()
// P-DAG: define internal {{.*}}ptr @thread.wasm.callback(ptr
// P-DAG: define internal void @handler.wasm.callback(i32
// P-DAG: define internal void @forward.wasm.callback(i32
// P-DAG: define internal void @cleanup.wasm.callback(ptr
// P-DAG: define internal {{.*}}i32 @compare.wasm.callback(ptr {{.*}}, ptr
// P-DAG: define internal {{.*}}i8 @small.wasm.callback(i8
// P-DAG: call {{.*}}i8 @small()

// N-DAG: define internal {{.*}}i32 @opaque()
// N-DAG: call i32 %{{.*}}(i32 noundef 17)
// N-DAG: define internal void @conflict()
// N-DAG: define {{.*}}ptr @external()
// N-DAG: define internal void @aggregate_parameter()
// N-DAG: define internal void @aggregate_result(ptr {{.*}}sret
// N-DAG: define internal {{.*}}i32 @consumed_result()
// N-DAG: define internal void @missing_result()
// N-DAG: define internal void @nonzero(i32
// N-DAG: define internal void @variadic()
// N-DAG: define internal void @erased()
// N-DAG: define internal void @erased_after_prototype()
// N-DAG: define internal void @prototype()
// N-DAG: define internal {{.*}}ptr @direct_result()
// N-DAG: define internal void @retained()
// N-DAG: define internal void @untyped_consumer()
// N-DAG: define internal void @vararg_consumer()
// N-DAG: define internal void @conditional_erased()
// N-DAG: define internal void @generic_erased()
// N-DAG: define internal void @declared_before()
// N-DAG: define internal void @declared_after()
// N-DAG: define internal void @roundtrip_void()
// N-DAG: define internal void @roundtrip_unprototyped()
// N-DAG: define internal {{.*}}i32 @direct_consumed()
// N-DAG: define internal {{.*}}i32 @stmt_expr_consumed()

// N-DAG: call void @take_void(ptr {{.*}}@initializer)
// N-DAG: call void @take_void(ptr {{.*}}@at_exit)
// N-DAG: define internal {{.*}}ptr @initializer()
// N-DAG: define internal {{.*}}i32 @at_exit()
// OPT-DAG: define internal {{.*}}@thread() {{.*}}#[[BODY:[0-9]+]]
// OPT-DAG: define internal {{.*}}ptr @thread.wasm.callback(ptr
// OPT: attributes #[[BODY]] = { {{.*}}noinline
// REJECTED-LABEL: define {{.*}}i32 @fold(
// REJECTED: ret i32 8
// REJECTED-LABEL: define {{.*}}i32 @cast_call(
// REJECTED: call void @d()
// REJECTED: call i32 @e()
// REJECTED-NOT: call void @d(i32
// REJECTED-NOT: call i32 @e(i32
// ASM: define internal void @with_asm()
// NATIVE: define internal {{.*}}ptr @thread()
// PROTO: define{{.*}}i32 @{{.*}}ordinary

// COVER-DAG: @designated = {{.*}}global {{.*}}{ i32 1, {{.*}}ptr @tick.wasm.callback }
// COVER-DAG: @array = {{.*}}global [2 x ptr] [ptr @tick.wasm.callback, ptr null]
// COVER-DAG: define internal {{.*}}i32 @onintr()
// COVER-DAG: call {{.*}}i32 @onintr()
// COVER-DAG: call void @take(ptr {{.*}}@onintr)
// COVER-DAG: define internal void @tick.wasm.callback(i32
// COVER-DAG: define internal void @left.wasm.callback(i32
// COVER-DAG: define internal void @right.wasm.callback(i32
// COVER-DAG: define internal void @addr_left.wasm.callback(i32
// COVER-DAG: define internal void @addr_right.wasm.callback(i32
// COVER-DAG: define internal void @condition.wasm.callback(i32
// WEAK: store volatile ptr @worker,
// LABEL: store volatile ptr @worker,
// DTOR: call {{.*}}i32 @atexit(ptr @worker)
// ALIAS: @alias_name = internal alias void (), ptr @aliased
// SECTION-DAG: define internal void @section_worker() {{.*}}section ".text.callback"
// SECTION-DAG: define internal void @section_worker.wasm.callback(i32 {{.*}}section ".text.callback"
// DIAG: call void @take(ptr {{.*}}@warning_worker.wasm.callback)
// DIAG: define internal void @warning_worker.wasm.callback(i32
// AUDIT-ERROR: call void @take(ptr {{.*}}@warning_worker)
// AUDIT-ERROR: define internal void @warning_worker()

//--- positive.c
// System headers can contain redirects unrelated to the audited callbacks.
extern int redirected(void) __asm__("unrelated_redirect_target");
static void unrelated(void) {}
static void unrelated_weak(void) __attribute__((weakref("unrelated")));
static void unrelated_alias(void) __attribute__((alias("unrelated")));
typedef void *(*start_type)(void *);
extern void take_start(start_type);
extern void take_signal(void (*)(int));
extern void take_cleanup(void (*)(void *));
extern void take_compare(int (*)(const void *, const void *));
extern void take_small(signed char (*)(signed char));
static volatile int calls;
static void *thread() { calls++; return (void *)1; }
static void handler() {}
static void forward();
static void forward() {}
static void cleanup() {}
static int compare() { return 0; }
static signed char small() { return 4; }
start_type volatile saved = thread;
start_type addresses[2] = {thread, &thread};
struct pair { start_type callback; };
struct pair pair = {thread};
start_type get_start(void) { return thread; }
int install(void) {
  take_start(thread);
  take_start(&thread);
  take_signal(handler);
  take_signal(forward);
  take_signal(0 ? handler : handler);
  take_cleanup(cleanup);
  take_compare(compare);
  take_small((signed char (*)(signed char))small);
  // Direct calls still use the original body, including its result.
  (void)thread();
  return saved == thread;
}

//--- negative.c
struct aggregate { long words[4]; };
extern void take_int(void (*)(int));
extern void take_pointer(void (*)(void *));
extern void take_void(void (*)(void));
extern void take_double(double (*)(int));
extern void take_value(int (*)(int));
extern void take_aggregate(void (*)(struct aggregate));
extern void take_sret(void (*)(int));
extern void take_varargs(void (*)(int, ...));
extern void unknown(void *);
extern void take_untyped();
extern void take_variadic(int, ...);
static void *initializer() { return (void *)2; }
static int at_exit() { return 3; }
static int opaque() { return 7; }
int (*fp)() = opaque;
int call_opaque(void) { return fp(17); }
static void conflict() {}
void *external() { return (void *)1; }
static void aggregate_parameter() {}
static struct aggregate aggregate_result() { return (struct aggregate){{0}}; }
static int consumed_result() { return 1; }
static void missing_result() {}
static void nonzero(x) int x; {}
static void variadic() {}
static void erased() {}
static void erased_after_prototype() {}
static void prototype(void) {}
static void *direct_result() { return (void *)1; }
static void retained() __attribute__((used));
static void retained() {}
static void untyped_consumer() {}
static void vararg_consumer() {}
static void conditional_erased() {}
static void generic_erased() {}
static void declared_before(void);
static void declared_before() {}
static void declared_after() {}
static void declared_after(void);
static void roundtrip_void() {}
static void roundtrip_unprototyped() {}
static int direct_consumed() { return 42; }
static int stmt_expr_consumed() { return 43; }
int consume_stmt_expr_result(void) {
  take_int((void (*)(int))stmt_expr_consumed);
  return ({ stmt_expr_consumed(); });
}
int consume_direct_result(void) {
  take_int((void (*)(int))direct_consumed);
  return direct_consumed();
}
void visible_roundtrips(void) {
  take_int(roundtrip_void);
  ((void (*)(void))(void (*)(int))roundtrip_void)();
  take_int(roundtrip_unprototyped);
  ((void (*)())(void (*)(int))roundtrip_unprototyped)();
}
void *negative(void) {
  take_void((void *)initializer);
  take_void((void (*)(void))at_exit);
  take_int(conflict);
  take_pointer(conflict);
  take_pointer((void (*)(void *))external);
  take_aggregate(aggregate_parameter);
  take_sret((void (*)(int))aggregate_result);
  take_double((double (*)(int))consumed_result);
  take_value((int (*)(int))missing_result);
  take_int(nonzero);
  take_varargs((void (*)(int, ...))variadic);
  take_int(erased);
  unknown((void *)erased);
  take_int(erased_after_prototype);
  unknown((void *)(void (*)(int))erased_after_prototype);
  take_int((void (*)(int))prototype);
  take_void((void (*)(void))direct_result);
  take_int(retained);
  take_int(untyped_consumer);
  take_untyped((void (*)(int))untyped_consumer);
  take_int(vararg_consumer);
  take_variadic(0, (void (*)(int))vararg_consumer);
  take_int(conditional_erased);
  unknown((void *)(0 ? (void (*)(int))conditional_erased : 0));
  take_int(generic_erased);
  unknown((void *)_Generic(0, int: (void (*)(int))generic_erased));
  take_int((void (*)(int))declared_before);
  take_int((void (*)(int))declared_after);
  return direct_result();
}

//--- asm.c
extern void take_int(void (*)(int));
static void with_asm() { __asm__("" : : : "memory"); }
void install_asm(void) { take_int(with_asm); }

//--- prototyped.c
static int callback(void) { return 1; }
int ordinary(void) {
  int (*volatile fp)(void) = callback;
  return fp();
}

//--- coverage.c
typedef void (*slot)(int);
extern void take(slot);
static void tick() {}
struct ops { int id; slot on; };
struct ops designated = {.on = tick, .id = 1};
slot array[2] = {[0] = tick};
static onintr() { return 0; }
static void left() {}
static void right() {}
static void addr_left() {}
static void addr_right() {}
static void condition() {}
int coverage(slot local, int n) {
  if (n < 0) onintr();
  (void)onintr();
  ({ onintr(); });
  n ? onintr() : 0;
  switch (n) {
  case 0: onintr(); break;
  default: onintr();
  }
label: onintr();
  take(onintr);
  take(left);
  take(right);
  take(addr_left);
  take(addr_right);
  take(condition ? condition : condition);
  return (left == local) + (local == right) +
         (&addr_left != local) + (local != &addr_right);
}

//--- weakref.c
extern void take(void (*)(int));
static void worker() {}
static void alt(void) __attribute__((weakref("worker")));
void weakref_use(void) {
  void (*volatile f)(void) = alt;
  take(worker);
  f();
}

//--- asmlabel.c
extern void take(void (*)(int));
static void worker() {}
#ifdef PRAGMA
#pragma redefine_extname alt worker
extern void alt(void);
#else
extern void alt(void) __asm__("worker");
#endif
void label_use(void) {
  void (*volatile f)(void) = alt;
  take(worker);
  f();
}

//--- dtor.c
extern void take(void (*)(int));
static void worker() __attribute__((destructor));
static void worker() {}
void install(void) { take(worker); }

//--- alias.c
extern void take(void (*)(int));
static void aliased() {}
static void alias_name(void) __attribute__((alias("aliased")));
void install_alias(void) {
  take(aliased);
  take((void (*)(int))alias_name);
}

//--- rejected.c
static void a() {}
static void b() {}
static void d() {}
static int e() { return 2; }
void (*legacy)() = a;
void (*tab[2])() = {d, 0};
int fold(void) {
  void *erase = (void *)b;
  return a ? 8 : 0;
}
int cast_call(void) {
  ((void (*)(int))d)(1);
  return ((int (*)(int, int))e)(1, 2);
}

//--- section.c
#pragma clang section text = ".text.callback"
static void section_worker() {}
#pragma clang section text = ""
extern void take(void (*)(int));
void install_section(void) { take(section_worker); }

//--- diagnostics.c
extern void take(void (*)(int));
static void warning_worker() {}
struct big { char c[32]; };
struct big value;
// Command-line warning policy is replayed, but source pragma mappings belong
// to the production engine. With -Werror the audit alone errors here; it must
// reject adaptation without failing the successful stock compilation.
#ifdef AUDIT_ERROR
#pragma clang diagnostic ignored "-Watomic-alignment"
#endif
void install_warning(void) {
  take(warning_worker);
  struct big result;
  __atomic_load(&value, &result, __ATOMIC_SEQ_CST); // expected-warning {{misaligned atomic operation}} expected-warning {{large atomic operation}}
}
