# FIREBOX PATCH (firebox#CTX): `-soname` is the wasm analog of ELF DT_SONAME.
# `wasm-ld -soname=N` writes a `firebox.soname` custom section; a module linked
# against a shared object carrying one records N in its dylink.0 NEEDED, and the
# input's filename when it carries none (upstream behavior, unchanged).

# RUN: rm -rf %t.dir && mkdir -p %t.dir/named %t.dir/plain %t.dir/stripped
# RUN: llvm-mc -filetype=obj -triple=wasm32-unknown-unknown -o %t.o %s
# RUN: llvm-mc -filetype=obj -triple=wasm32-unknown-unknown %p/Inputs/ret32.s -o %t.ret32.o

# The provider: linked under the development name, carrying its real name.
# RUN: wasm-ld -shared --experimental-pic -soname=libret32-g0123456789abcdef.so \
# RUN:   -o %t.dir/named/libret32.so %t.ret32.o
# RUN: obj2yaml %t.dir/named/libret32.so | FileCheck %s --check-prefix=PROVIDER
# --strip-all keeps it: it is linking semantics, not debug information.
# RUN: wasm-ld -shared --experimental-pic --strip-all -soname=libret32-g0123456789abcdef.so \
# RUN:   -o %t.dir/stripped/libret32.so %t.ret32.o
# RUN: obj2yaml %t.dir/stripped/libret32.so | FileCheck %s --check-prefix=PROVIDER
# The control: the same object with no soname.
# RUN: wasm-ld -shared --experimental-pic -o %t.dir/plain/libret32.so %t.ret32.o
# RUN: obj2yaml %t.dir/plain/libret32.so | FileCheck %s --check-prefix=PLAIN-PROVIDER

# `-l` through the search path binds the soname, not the file found.
# RUN: wasm-ld -shared --experimental-pic -o %t.named-l.so %t.o -L%t.dir/named -lret32
# RUN: obj2yaml %t.named-l.so | FileCheck %s --check-prefix=BINDS-SONAME
# So does naming the file by path, and so does the stripped provider.
# RUN: wasm-ld -shared --experimental-pic -o %t.named-path.so %t.o %t.dir/named/libret32.so
# RUN: obj2yaml %t.named-path.so | FileCheck %s --check-prefix=BINDS-SONAME
# RUN: wasm-ld -shared --experimental-pic -o %t.stripped-l.so %t.o -L%t.dir/stripped -lret32
# RUN: obj2yaml %t.stripped-l.so | FileCheck %s --check-prefix=BINDS-SONAME
# A PIE main binds it the same way.
# RUN: wasm-ld -pie --experimental-pic --no-entry -o %t.named.wasm %t.o -L%t.dir/named -lret32
# RUN: obj2yaml %t.named.wasm | FileCheck %s --check-prefix=BINDS-SONAME

# Without a soname, NEEDED is the input's filename: upstream behavior, unchanged.
# RUN: wasm-ld -shared --experimental-pic -o %t.plain-l.so %t.o -L%t.dir/plain -lret32
# RUN: obj2yaml %t.plain-l.so | FileCheck %s --check-prefix=BINDS-FILENAME

# A malformed section is an error, never a silent fallback to the filename.
# RUN: cp %t.dir/named/libret32.so %t.bad.so
# RUN: %python -c "import sys; p=sys.argv[1]; d=bytearray(open(p,'rb').read()); i=d.rindex(b'firebox.soname')+len(b'firebox.soname'); d[i]=0x7f; open(p,'wb').write(bytes(d))" %t.bad.so
# RUN: not wasm-ld -shared --experimental-pic -o %t.bad-consumer.so %t.o %t.bad.so 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MALFORMED

.globl foo

.functype ret32 (f32) -> (i32)

foo:
  .functype foo (f32) -> (i32)
  local.get 0
  call ret32
  end_function

# PROVIDER:      Name:            firebox.soname
# PROVIDER-NEXT: Payload:         1D6C696272657433322D67303132333435363738396162636465662E736F

# PLAIN-PROVIDER-NOT: firebox.soname

# BINDS-SONAME:      Name:            dylink.0
# BINDS-SONAME:      Needed:
# BINDS-SONAME-NEXT:   - libret32-g0123456789abcdef.so
# BINDS-SONAME-NEXT: - Type:
# BINDS-SONAME-NOT:  firebox.soname

# BINDS-FILENAME:      Name:            dylink.0
# BINDS-FILENAME:      Needed:
# BINDS-FILENAME-NEXT:   - libret32.so
# BINDS-FILENAME-NEXT: - Type:

# MALFORMED: malformed firebox.soname section
