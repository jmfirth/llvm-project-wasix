// RUN: split-file %s %t
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/positive.c | FileCheck %s --check-prefix=P
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/positive.c | FileCheck %s --check-prefix=P
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/negative.c | FileCheck %s --check-prefix=N --implicit-check-not=.legacy.body
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/negative.c | FileCheck %s --check-prefix=N --implicit-check-not=.legacy.body
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/asm.c | FileCheck %s --check-prefix=ASM --implicit-check-not=.legacy.body
// RUN: %clang_cc1 -triple x86_64-unknown-linux -std=gnu99 -emit-llvm -disable-llvm-passes -o - %t/positive.c | FileCheck %s --check-prefix=NATIVE --implicit-check-not=.legacy.body
// RUN: %clang_cc1 -triple wasm32-unknown-wasi -std=c23 -emit-llvm -o - %t/prototyped.c | FileCheck %s --check-prefix=PROTO --implicit-check-not=.legacy.body
// RUN: %clang_cc1 -triple wasm64-unknown-wasi -std=c++17 -x c++ -emit-llvm -o - %t/prototyped.c | FileCheck %s --check-prefix=PROTO --implicit-check-not=.legacy.body

// All address expressions, including initializers, address-of, casts, and
// comparisons, must refer to ONE canonical symbol. No API spelling selects it.
// P-DAG: @saved = {{.*}}global ptr @thread
// P-DAG: @addresses = {{.*}}global [2 x ptr] [ptr @thread, ptr @thread]
// P-DAG: @pair = {{.*}}global %struct.pair { ptr @thread }
// P-DAG: ret ptr @thread
// P-DAG: call void @take_start(ptr {{.*}}@thread)
// P-DAG: call void @take_signal(ptr {{.*}}@handler)
// P-DAG: call void @take_cleanup(ptr {{.*}}@cleanup)
// P-DAG: call void @take_once(ptr {{.*}}@initializer)
// P-DAG: call void @take_compare(ptr {{.*}}@compare)
// P-DAG: call void @take_exit(ptr {{.*}}@at_exit)
// P-DAG: icmp eq ptr {{.*}}, @thread
// P-DAG: call {{.*}}ptr @thread.legacy.body()
// P-DAG: define internal {{.*}}ptr @thread(ptr
// P-DAG: define internal void @handler(i32
// P-DAG: define internal void @forward(i32
// P-DAG: define internal void @cleanup(ptr
// P-DAG: define internal void @initializer()
// P-DAG: define internal {{.*}}i32 @compare(ptr {{.*}}, ptr
// P-DAG: define internal void @at_exit()
// P-DAG: define internal {{.*}}i8 @small(i8
// P-DAG: call {{.*}}i8 @small.legacy.body()

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
// N-DAG: define internal void @aliased()
// N-DAG: define internal void @retained()
// N-DAG: define internal void @untyped_consumer()
// N-DAG: define internal void @vararg_consumer()
// N-DAG: define internal void @conditional_erased()
// N-DAG: define internal void @generic_erased()
// N-DAG: define internal void @declared_before()
// N-DAG: define internal void @declared_after()

// ASM: define internal void @with_asm()
// NATIVE: define internal {{.*}}ptr @thread()
// PROTO: define{{.*}}i32 @{{.*}}ordinary

//--- positive.c
typedef void *(*start_type)(void *);
extern void take_start(start_type);
extern void take_signal(void (*)(int));
extern void take_cleanup(void (*)(void *));
extern void take_once(void (*)(void));
extern void take_compare(int (*)(const void *, const void *));
extern void take_exit(void (*)(void));
extern void take_small(signed char (*)(signed char));
static void *thread() { return (void *)1; }
static void handler() {}
static void forward();
static void forward() {}
static void cleanup() {}
static void *initializer() { return (void *)2; }
static int compare() { return 0; }
static int at_exit() { return 3; }
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
  take_once((void *)initializer);
  take_compare(compare);
  take_exit((void (*)(void))at_exit);
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
static void aliased() {}
static void alias_name() __attribute__((alias("aliased")));
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
void *negative(void) {
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
  take_int(aliased);
  take_int(alias_name);
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
