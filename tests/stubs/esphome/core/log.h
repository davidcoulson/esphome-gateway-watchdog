#pragma once
namespace esphome { void test_log(char level, const char *tag, const char *fmt, ...); }
#define ESP_LOGE(tag, ...) ::esphome::test_log('E', tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) ::esphome::test_log('W', tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) ::esphome::test_log('I', tag, __VA_ARGS__)
#define ESP_LOGCONFIG(tag, ...) ::esphome::test_log('C', tag, __VA_ARGS__)
