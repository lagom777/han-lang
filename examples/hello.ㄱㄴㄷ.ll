; 나/가나다 → LLVM IR (subset) + na_rt
target triple = "arm64-apple-macosx"

@.s0 = private unnamed_addr constant [19 x i8] c"\EC\95\88\EB\85\95, \EA\B0\80\EB\82\98\EB\8B\A4!\00"
@.s1 = private unnamed_addr constant [7 x i8] c"\EC\84\B8\EA\B3\84\00"
@.s2 = private unnamed_addr constant [9 x i8] c"\EC\95\88\EB\85\95, \00"
@.s3 = private unnamed_addr constant [2 x i8] c"!\00"

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
  %0 = getelementptr inbounds [19 x i8], ptr @.s0, i64 0, i64 0
  call void @na_print_cstr(ptr %0)
  %1 = add i64 0, 0
  %2 = getelementptr inbounds [7 x i8], ptr @.s1, i64 0, i64 0
  store ptr %2, ptr %sloc1, align 8
  %3 = getelementptr inbounds [9 x i8], ptr @.s2, i64 0, i64 0
  %4 = load ptr, ptr %sloc1, align 8
  %5 = call ptr @na_str_concat(ptr %3, ptr %4)
  %6 = getelementptr inbounds [2 x i8], ptr @.s3, i64 0, i64 0
  %7 = call ptr @na_str_concat(ptr %5, ptr %6)
  call void @na_print_cstr(ptr %7)
  %8 = add i64 0, 0
  ret i32 0
}
