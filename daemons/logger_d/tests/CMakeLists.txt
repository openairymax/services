# logger_d 单元测试：配置装载与落盘轮转
add_executable(logd_cfg_test
	logd_cfg_test.c
)

target_link_libraries(logd_cfg_test
	PRIVATE
		airy_logd
)

# 出厂配置必须与内置默认值逐键一致，路径由构建期注入，避免测试里再抄一份
target_compile_definitions(logd_cfg_test
	PRIVATE
		LOGD_SHIPPED_CONF="${CMAKE_CURRENT_SOURCE_DIR}/../etc/logger.conf"
)

add_test(NAME logd_cfg_test COMMAND logd_cfg_test)

add_executable(logd_file_test
	logd_file_test.c
)

target_link_libraries(logd_file_test
	PRIVATE
		airy_logd
)

add_test(NAME logd_file_test COMMAND logd_file_test)
