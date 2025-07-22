namespace ur {
namespace test_target_functions {

__attribute__((noinline)) int hook_func() {
    return 200;
}

__attribute__((noinline)) int original_target_func() {
    return 50;
}

} // namespace test_target_functions
} // namespace ur
