# macro_d 单元测试：配置装载与内置默认值
add_executable(macro_cfg_test
	macro_cfg_test.c
)

target_link_libraries(macro_cfg_test
	PRIVATE
		airy_macro
)

# 出厂配置必须与内置默认值逐键一致，路径由构建期注入，避免测试里再抄一份
target_compile_definitions(macro_cfg_test
	PRIVATE
		MACRO_SHIPPED_CONF="${CMAKE_CURRENT_SOURCE_DIR}/../etc/macro-superv.conf"
)

add_test(NAME macro_cfg_test COMMAND macro_cfg_test)
