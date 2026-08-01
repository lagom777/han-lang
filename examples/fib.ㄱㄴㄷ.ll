; 나/가나다 → LLVM IR (subset) + na_rt
target triple = "arm64-apple-macosx"

@.s0 = private unnamed_addr constant [14 x i8] c"\ED\94\BC\EB\B3\B4\EB\82\98\EC\B9\98(\00"
@.s1 = private unnamed_addr constant [5 x i8] c") = \00"

declare void @na_print_i64(i64)
declare void @na_print_cstr(ptr)
declare ptr @na_str_concat(ptr, ptr)
declare ptr @na_i64_to_str(i64)

define i64 @"\ED\94\BC\EB\B3\B4\EB\82\98\EC\B9\98"(i64 %arg0) {
entry:
  %loc0 = alloca i64, align 8
  store i64 0, ptr %loc0, align 8
  %sloc0 = alloca ptr, align 8
  store ptr null, ptr %sloc0, align 8
  %loc1 = alloca i64, align 8
  store i64 0, ptr %loc1, align 8
  %sloc1 = alloca ptr, align 8
  store ptr null, ptr %sloc1, align 8
  store i64 %arg0, ptr %loc0, align 8
  %0 = load i64, ptr %loc0, align 8
  %1 = add i64 0, 2
  %2 = icmp slt i64 %0, %1
  br i1 %2, label %L0, label %L2
L0:
  %3 = load i64, ptr %loc0, align 8
  ret i64 %3
L2:
  %4 = load i64, ptr %loc0, align 8
  %5 = add i64 0, 1
  %6 = sub i64 %4, %5
  %7 = call i64 @"\ED\94\BC\EB\B3\B4\EB\82\98\EC\B9\98"(i64 %6)
  %8 = load i64, ptr %loc0, align 8
  %9 = add i64 0, 2
  %10 = sub i64 %8, %9
  %11 = call i64 @"\ED\94\BC\EB\B3\B4\EB\82\98\EC\B9\98"(i64 %10)
  %12 = add i64 %7, %11
  ret i64 %12
}

define i32 @main() {
entry:
  %loc0 = alloca i64, align 8
  store i64 0, ptr %loc0, align 8
  %sloc0 = alloca ptr, align 8
  store ptr null, ptr %sloc0, align 8
  %loc1 = alloca i64, align 8
  store i64 0, ptr %loc1, align 8
  %sloc1 = alloca ptr, align 8
  store ptr null, ptr %sloc1, align 8
  %loc2 = alloca i64, align 8
  store i64 0, ptr %loc2, align 8
  %sloc2 = alloca ptr, align 8
  store ptr null, ptr %sloc2, align 8
  %0 = add i64 0, 0
  %1 = add i64 0, 10
  %2 = add i64 0, 1
  store i64 %0, ptr %loc0, align 8
  br label %L0
L0:
  %3 = load i64, ptr %loc0, align 8
  %4 = icmp sle i64 %3, %1
  br i1 %4, label %L1, label %L3
L1:
  %5 = getelementptr inbounds [14 x i8], ptr @.s0, i64 0, i64 0
  %6 = load i64, ptr %loc0, align 8
  %7 = call ptr @na_i64_to_str(i64 %6)
  %8 = call ptr @na_str_concat(ptr %5, ptr %7)
  %9 = getelementptr inbounds [5 x i8], ptr @.s1, i64 0, i64 0
  %10 = call ptr @na_str_concat(ptr %8, ptr %9)
  %11 = load i64, ptr %loc0, align 8
  %12 = call i64 @"\ED\94\BC\EB\B3\B4\EB\82\98\EC\B9\98"(i64 %11)
  %13 = call ptr @na_i64_to_str(i64 %12)
  %14 = call ptr @na_str_concat(ptr %10, ptr %13)
  call void @na_print_cstr(ptr %14)
  %15 = add i64 0, 0
  br label %L2
L2:
  %16 = load i64, ptr %loc0, align 8
  %17 = add i64 %16, %2
  store i64 %17, ptr %loc0, align 8
  br label %L0
L3:
  ret i32 0
}
