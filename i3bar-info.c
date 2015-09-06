#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/sysinfo.h>
#include <limits.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#include <netdb.h>

#define LIKELY(b)   __builtin_expect(!!(b), 1)
#define UNLIKELY(b) __builtin_expect(!!(b), 0)

#define free(x) free((void *)(x))

static void
print_header(void)
{
    puts("{\"version\":1}\n[");
}

struct section_data {
    const char *name;
    const char *instance;
    const char *color;
    const char *full_text;
    const char *short_text;
    const char *min_width;
    int separator_block_width;
    int disabled;
    enum {
        URGENT_DEFAULT = 0,
        URGENT_YES,
        URGENT_NO
    } urgent;
    enum {
        SEPARATOR_DEFAULT = 0,
        SEPARATOR_YES,
        SEPARATOR_NO
    } separator;
    enum {
        ALIGN_DEFAULT = 0,
        ALIGN_LEFT,
        ALIGN_CENTER,
        ALIGN_RIGHT
    } align;
    enum {
        MARKUP_DEFAULT = 0,
        MARKUP_NONE,
        MARKUP_PANGO
    } markup;
};

typedef int (*section_func)(struct section_data *);

static int
sprintf_s(char **text, const char *fmt, ...)
{
    int err, len;
    va_list vl;

    va_start(vl, fmt);
    len = vsnprintf(*text, 0, fmt, vl);
    if (len < 0) {
        return -1;
    }
    va_end(vl);

    ++len;
    *text = malloc((unsigned int)len * sizeof(**text));
    if (*text == NULL) {
        return -1;
    }

    va_start(vl, fmt);
    err = vsnprintf(*text, (unsigned int)len, fmt, vl);
    if (err < len - 1) {
        free(*text);
        *text = NULL;
        return -1;
    }
    va_end(vl);

    return 0;
}

static void
tz(void)
{
    static int b = 1;
    if (UNLIKELY(b)) {
        tzset();
        b = 0;
    }
}

static int
datetime(struct section_data *data)
{
    time_t t;
    struct tm tm;
    char *text;
    int err;

    t = time(NULL);
    if (t == (time_t)-1) {
        return -1;
    }
    tz();
    if (localtime_r(&t, &tm) == NULL) {
        return -1;
    }

    err = sprintf_s(&text, "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900,
        tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    if (err < 0) {
        return -1;
    }

    data->name = "datetime";
    data->full_text = text;

    return 0;
}

static int
free_datetime(struct section_data *data)
{
    free(data->full_text);
    return 0;
}

static int n_processors = 0;

static int
cpu_load(struct section_data *data)
{
    double avgs[3];
    char *text;
    int err;

    err = getloadavg(avgs, 3);
    if (err != 3) {
        return -1;
    }

    err = sprintf_s(&text, "%.2f %.2f %.2f", avgs[0], avgs[1], avgs[2]);
    if (err < 0) {
        return -1;
    }

    data->name = "cpu_load";
    data->full_text = text;

    if (n_processors > 0) {
        if (avgs[0] > n_processors + 1
            || avgs[1] > n_processors
            || avgs[2] > n_processors - 1) {
            data->color = "#ffff00";
        }
        if (avgs[0] > n_processors * 2 + 1
            || avgs[1] > n_processors * 2
            || avgs[2] > n_processors + 1) {
            data->color = "#ff0000";
        }
    }

    return 0;
}

static int
free_cpu_load(struct section_data *data)
{
    free(data->full_text);
    return 0;
}

static int
battery(struct section_data *data)
{
    FILE *f;
    char *text, *p;
    size_t len;

    text = malloc(5 * sizeof(char));
    if (!text) {
        return -1;
    }

    f = fopen("/sys/class/power_supply/BAT1/capacity", "r");
    if (f == NULL) {
        free(text);
        return -1;
    }

    len = fread(text, sizeof(char), 5, f);
    if (len < 1 || len > 4 || ferror(f) || !feof(f)) {
        fclose(f);
        free(text);
        return -1;
    }
    fclose(f);

    p = strchr(text, '\n');
    if (p == NULL) {
        free(text);
        return -1;
    }
    p[0] = '%';
    p[1] = '\0';

    data->name = "battery";
    data->full_text = text;

    return 0;
}

static int
free_battery(struct section_data *data)
{
    free(data->full_text);
    return 0;
}

static int
cpu_temp(struct section_data *data)
{
    FILE *f;
    char buf[8] = {0};
    size_t len;
    long temp;
    char *text;
    int err;

    f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (f == NULL) {
        return -1;
    }

    len = fread(buf, sizeof(char), 8, f);
    assert(len <= 8);
    if (len == 8 || ferror(f) || !feof(f)) {
        fclose(f);
        return -1;
    }
    fclose(f);

    temp = strtol(buf, NULL, 10);
    if (temp == 0 || temp == LONG_MIN || temp == LONG_MAX) {
        return -1;
    }

    temp /= 1000;

    err = sprintf_s(&text, "%ld°C", temp);
    if (err < 0) {
        return -1;
    }

    if (temp > 75) {
        data->color = "#ff0000";
    } else if (temp > 70) {
        data->color = "#ffff00";
    }

    data->name = "cpu_temp";
    data->full_text = text;

    return 0;
}

static int
free_cpu_temp(struct section_data *data)
{
    free(data->full_text);
    return 0;
}

static int
ip(struct section_data *data)
{
    int err;
    struct ifaddrs *ifaddrs;
    char host[512];

    err = getifaddrs(&ifaddrs);
    if (err < 0) {
        return -1;
    }

    for (struct ifaddrs *p = ifaddrs; p != NULL; p = p->ifa_next) {
        if (p->ifa_addr == NULL || p->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        if (!strncasecmp("lo", p->ifa_name, 2)) {
            continue;
        }

        // This is the check i3status makes.
        // TODO - check for IFF_UP?
        if (!(p->ifa_flags & IFF_RUNNING)) {
            continue;
        }

        err = getnameinfo(p->ifa_addr, sizeof(*p->ifa_addr), host,
            sizeof(host), NULL, 0, NI_NUMERICHOST);
        if (err != 0) {
            goto error;
        }

        data->full_text = strdup(host);
        if (data->full_text == NULL) {
            goto error;
        }
        data->name = "ip";

        freeifaddrs(ifaddrs);
        return 0;
    }

error:
    freeifaddrs(ifaddrs);
    return -1;
}

static int
free_ip(struct section_data *data)
{
    free(data->full_text);
    return 0;
}

static void
print_data(struct section_data *data)
{
    int comma = 0;

    putchar('{');

#define COMMA() do { \
    if (comma) { \
        putchar(','); \
    } else { \
        comma = 1; \
    } \
} while (0)

#define PRINT_STRING(name) do { \
    if (data->name) { \
        COMMA(); \
        printf("\"" #name "\":\"%s\"", data->name); \
    } \
} while (0)

    PRINT_STRING(name);
    PRINT_STRING(instance);
    PRINT_STRING(color);
    PRINT_STRING(full_text);
    PRINT_STRING(short_text);
    PRINT_STRING(min_width);

    if (data->separator_block_width >= 0) {
        COMMA();
        printf("\"separator_block_width\":%d", data->separator_block_width);
    }

    if (data->urgent != URGENT_DEFAULT) {
        COMMA();
        printf("\"urgent\":%s", data->urgent == URGENT_YES ? "true" :
            "false");
    }

    if (data->separator != SEPARATOR_DEFAULT) {
        COMMA();
        printf("\"separator\":%s", data->separator == SEPARATOR_NO ? "false"
            : "true");
    }

    if (data->align != ALIGN_DEFAULT) {
        COMMA();
        printf("\"align\":%s", data->align == ALIGN_RIGHT ? "right" :
            data->align == ALIGN_CENTER ? "center" : "left");
    }

    if (data->markup != MARKUP_DEFAULT) {
        COMMA();
        printf("\"markup\":\"%s\"", data->markup == MARKUP_PANGO ? "pango" :
            "none");
    }

    putchar('}');

#undef PRINT_STRING
#undef COMMA
}

struct section {
    section_func func;
    section_func free_func;
};

static void
print_all_data(void)
{
    const struct section sections[] = {
        {ip, free_ip},
        {cpu_temp, free_cpu_temp},
        {cpu_load, free_cpu_load},
        {battery, free_battery},
        {datetime, free_datetime}
    };

    for (unsigned int i = 0; i < sizeof(sections) / sizeof(sections[0]); ++i) {
        int err;
        struct section_data data;

        memset(&data, 0, sizeof(data));
        data.separator_block_width = -1;
        assert(data.name == NULL);
        assert(data.instance == NULL);
        assert(data.color == NULL);
        assert(data.full_text == NULL);
        assert(data.short_text == NULL);
        assert(data.min_width == NULL);
        assert(data.separator_block_width < 0);
        assert(data.urgent == URGENT_DEFAULT);
        assert(data.separator == SEPARATOR_DEFAULT);
        assert(!data.disabled);
        assert(data.align == ALIGN_DEFAULT);
        assert(data.markup == MARKUP_DEFAULT);

        err = sections[i].func(&data);
        if (err == 0 && !data.disabled) {
            if (i > 0) {
                putchar(',');
            }
            print_data(&data);
        }
        err = sections[i].free_func(&data);
        assert(err == 0);
    }
}

int
main(void)
{
    n_processors = get_nprocs();
    assert(n_processors > 0);

    print_header();

    // These gotos allow for an efficient solution to the comma problem without
    // duplicating code.
    putchar('[');
    goto print;
loop:
    fputs(",[", stdout);
print:
    print_all_data();
    puts("]");
    fflush(stdout);
    sleep(1);
    goto loop;

    // Probably won't get here
    puts("]");
    return 0;
}
