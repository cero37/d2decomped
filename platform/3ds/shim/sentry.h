// SPDX-License-Identifier: GPL-3.0-or-later
// 3DS: no crash reporter. The calls main.cpp makes, as no-ops.
#pragma once
#include <cstddef>
struct sentry_options_t {};
struct sentry_value_t { int unused; };
struct sentry_uuid_t { char bytes[16]; };
struct sentry_attachment_t;
inline sentry_options_t* sentry_options_new() { static sentry_options_t options; return &options; }
inline void sentry_options_set_dsn(sentry_options_t*, const char*) {}
inline void sentry_options_set_release(sentry_options_t*, const char*) {}
inline void sentry_options_set_auto_session_tracking(sentry_options_t*, int) {}
inline void sentry_options_set_database_path(sentry_options_t*, const char*) {}
inline void sentry_options_set_handler_path(sentry_options_t*, const char*) {}
inline void sentry_options_set_debug(sentry_options_t*, int) {}
inline int sentry_init(sentry_options_t*) { return 1; }
inline int sentry_close() { return 0; }
inline bool sentry_is_enabled() { return false; }
inline sentry_value_t sentry_value_new_object() { return {}; }
inline sentry_value_t sentry_value_new_string(const char*) { return {}; }
inline sentry_value_t sentry_value_new_string_n(const char*, std::size_t) { return {}; }
inline sentry_value_t sentry_value_new_int32(int) { return {}; }
inline sentry_value_t sentry_value_new_breadcrumb(const char*, const char*) { return {}; }
inline int sentry_value_set_by_key(sentry_value_t, const char*, sentry_value_t) { return 0; }
inline int sentry_value_is_null(sentry_value_t) { return 1; }
inline void sentry_add_breadcrumb(sentry_value_t) {}
inline void sentry_set_context(const char*, sentry_value_t) {}
inline void sentry_set_extra(const char*, sentry_value_t) {}
inline void sentry_set_tag(const char*, const char*) {}
inline void sentry_crash() {}
inline int sentry_uuid_is_nil(const sentry_uuid_t*) { return 1; }
inline sentry_value_t sentry_attachment_from_bytes(const char*, std::size_t, const char*) { return {}; }
inline void sentry_attachment_set_content_type(sentry_value_t, const char*) {}
inline sentry_uuid_t sentry_add_attachment(sentry_value_t) { return {}; }
inline void sentry_remove_attachment(sentry_uuid_t) {}
