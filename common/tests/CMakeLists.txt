# ===========================================================================
# services/common/tests — daemon 框架单元测试（CTest 挂点）
# 用例以 TAP 13 输出，由 ctest 调度；不引入外部测试框架依赖。
# ===========================================================================

find_package(Threads REQUIRED)

add_executable(dmn_test dmn_test.c)

target_link_libraries(dmn_test
	PRIVATE
		airy_daemon
		Threads::Threads
)

add_test(NAME dmn_test COMMAND dmn_test)
set_tests_properties(dmn_test PROPERTIES TIMEOUT 120)
