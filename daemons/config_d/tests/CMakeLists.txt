# config_d 单元测试：装载、覆盖、校验、快照
add_executable(config_d_test
	config_d_test.c
)

target_link_libraries(config_d_test
	PRIVATE
		airy_cfg
)

add_test(NAME config_d_test COMMAND config_d_test)
