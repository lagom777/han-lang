; 나/가나다 → LLVM IR (subset) + na_rt
target triple = "arm64-apple-macosx"

@.s0 = private unnamed_addr constant [8 x i8] c"\ED\95\A9\EC\9D\B4 \00"
@.s1 = private unnamed_addr constant [16 x i8] c" \E2\80\94 \EB\A9\88\EC\B6\A4 (i=\00"
@.s2 = private unnamed_addr constant [2 x i8] c")\00"
@.s3 = private unnamed_addr constant [13 x i8] c"\EC\B5\9C\EC\A2\85 \ED\95\A9: \00"

declare void @na_print_i64(i64)
declare void @na_print_cstr(ptr)
declare ptr @na_str_concat(ptr, ptr)
declare ptr @na_i64_to_str(i64)

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
  store i64 %0, ptr %loc0, align 8
  %1 = add i64 0, 1
  %2 = add i64 0, 20
  %3 = add i64 0, 1
  store i64 %1, ptr %loc1, align 8
  br label %L0
L0:
  %4 = load i64, ptr %loc1, align 8
  %5 = icmp sle i64 %4, %2
  br i1 %5, label %L1, label %L3
L1:
  %6 = load i64, ptr %loc1, align 8
  %7 = add i64 0, 2
  %8 = srem i64 %6, %7
  %9 = add i64 0, 0
  %10 = icmp eq i64 %8, %9
  br i1 %10, label %L4, label %L6
L4:
  br label %L2
L6:
  %11 = load i64, ptr %loc0, align 8
  %12 = load i64, ptr %loc1, align 8
  %13 = add i64 %11, %12
  store i64 %13, ptr %loc0, align 8
  %14 = load i64, ptr %loc0, align 8
  %15 = add i64 0, 50
  %16 = icmp sgt i64 %14, %15
  br i1 %16, label %L7, label %L9
L7:
  %17 = getelementptr inbounds [8 x i8], ptr @.s0, i64 0, i64 0
  %18 = load i64, ptr %loc0, align 8
  %19 = call ptr @na_i64_to_str(i64 %18)
  %20 = call ptr @na_str_concat(ptr %17, ptr %19)
  %21 = getelementptr inbounds [16 x i8], ptr @.s1, i64 0, i64 0
  %22 = call ptr @na_str_concat(ptr %20, ptr %21)
  %23 = load i64, ptr %loc1, align 8
  %24 = call ptr @na_i64_to_str(i64 %23)
  %25 = call ptr @na_str_concat(ptr %22, ptr %24)
  %26 = getelementptr inbounds [2 x i8], ptr @.s2, i64 0, i64 0
  %27 = call ptr @na_str_concat(ptr %25, ptr %26)
  call void @na_print_cstr(ptr %27)
  %28 = add i64 0, 0
  br label %L3
L9:
  br label %L2
L2:
  %29 = load i64, ptr %loc1, align 8
  %30 = add i64 %29, %3
  store i64 %30, ptr %loc1, align 8
  br label %L0
L3:
  %31 = getelementptr inbounds [13 x i8], ptr @.s3, i64 0, i64 0
  %32 = load i64, ptr %loc0, align 8
  %33 = call ptr @na_i64_to_str(i64 %32)
  %34 = call ptr @na_str_concat(ptr %31, ptr %33)
  call void @na_print_cstr(ptr %34)
  %35 = add i64 0, 0
  ret i32 0
}
