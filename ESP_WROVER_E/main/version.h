#ifndef VERSION_H
#define VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#define APP_VERSION_GIT_HASH      GIT_COMMIT_HASH
#define APP_VERSION_TAG           GIT_TAG_VERSION
#define APP_VERSION_DATE          GIT_COMMIT_DATE
#define APP_VERSION_FULL          "v" APP_VERSION_TAG "-" APP_VERSION_GIT_HASH

const char *app_get_version_hash(void);
const char *app_get_version_tag(void);
const char *app_get_version_date(void);
const char *app_get_version_full(void);
void app_print_version_info(void);

#ifdef __cplusplus
}
#endif

#endif // VERSION_H