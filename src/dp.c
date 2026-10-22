/*
 * dp.c -- Dining-Philosophers Problem (F.CSM302, БД1, баг II-4-11)
 *
 * Сурах бичгийн (Operating System Concepts, 7-р бүлэг) Programming Project:
 * philosopher бүр thread, fork бүр нийтийн нөөц. Philosopher нь THINKING ->
 * HUNGRY -> EATING -> THINKING мөчлөгөөр ажиллана.
 *
 * Хоёр алгоритм:
 *   monitor : сурах бичгийн monitor шийдэл (pthread mutex + condition variable).
 *             Хөрш нь идэж байвал philosopher HUNGRY төлөвт хүлээнэ. Deadlock-гүй.
 *   naive   : philosopher бүр зүүн fork-оо, дараа нь баруун fork-оо авна.
 *             Бүгд зүүн fork-оо барьсан үед deadlock үүснэ (судалгааны зорилготой).
 *
 * Гаралт: stdout руу нэг мөр бүр нэг JSON event (JSON Lines). UI болон
 * tests/check_log.py энэ урсгалыг уншина. Алдаа, тусламж: stderr.
 *
 * Fork-ын дугаарлалт: fork k нь philosopher (k-1) ба philosopher k-ийн дунд.
 * Philosopher i нь зүүн fork i, баруун fork (i+1)%n-ийг ашиглана.
 *
 * Гарах код: 0 хэвийн, 2 буруу аргумент, 3 deadlock илэрсэн.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_PHIL 64
#define MAX_MS 600000

enum { THINKING, HUNGRY, EATING };
static const char *STATE_NAME[] = {"thinking", "hungry", "eating"};

typedef struct {
    int n, think_min, think_max, eat_min, eat_max;
    int duration_ms, grab_delay_ms, naive;
    unsigned seed;
} config_t;

static config_t cfg;
static struct timespec t0;
static atomic_int stop_flag;

/* ---------- Хугацаа ---------- */
static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec - t0.tv_sec) * 1000L + (t.tv_nsec - t0.tv_nsec) / 1000000L;
}

/* stop_flag тавигдвал таслагддаг sleep (5 мс алхамтай) */
static void sleep_ms(long ms) {
    long end = now_ms() + ms;
    while (!atomic_load(&stop_flag)) {
        long rem = end - now_ms();
        if (rem <= 0) break;
        struct timespec ts = {0, (rem > 5 ? 5 : rem) * 1000000L};
        nanosleep(&ts, NULL);
    }
}

static int rnd(unsigned *s, int lo, int hi) {
    return hi <= lo ? lo : lo + (int)(rand_r(s) % (unsigned)(hi - lo + 1));
}

/* ---------- Event лог + статистик (ev_lock-оор хамгаалагдана) ---------- */
static pthread_mutex_t ev_lock = PTHREAD_MUTEX_INITIALIZER;
static int state[MAX_PHIL];
static int fork_owner[MAX_PHIL];
static long hungry_since[MAX_PHIL], meals[MAX_PHIL];
static long wait_total[MAX_PHIL], wait_max[MAX_PHIL];

/*
 * Philosopher id-ийн төлөвийг st болгож, fork fa/fb-ийн эзэмшигчийг owner
 * болгоод (fa/fb < 0 бол алгасна) нэг event хэвлэнэ.
 */
static void log_state(int id, int st, int fa, int fb, int owner) {
    pthread_mutex_lock(&ev_lock);
    long t = now_ms();
    int prev = state[id];
    if (fa >= 0) fork_owner[fa] = owner;
    if (fb >= 0) fork_owner[fb] = owner;
    state[id] = st;
    if (st == HUNGRY && prev != HUNGRY) hungry_since[id] = t;
    if (st == EATING) {
        long w = t - hungry_since[id];
        wait_total[id] += w;
        if (w > wait_max[id]) wait_max[id] = w;
        meals[id]++;
    }
    printf("{\"type\":\"state\",\"t\":%ld,\"id\":%d,\"state\":\"%s\",\"forks\":[",
           t, id, STATE_NAME[st]);
    for (int k = 0; k < cfg.n; k++)
        printf("%d%s", fork_owner[k], k < cfg.n - 1 ? "," : "");
    printf("]}\n");
    fflush(stdout);
    pthread_mutex_unlock(&ev_lock);
}

/* ---------- Алгоритм 1: monitor (сурах бичгийн шийдэл) ---------- */
static pthread_mutex_t mon = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t self_cv[MAX_PHIL];
static int mstate[MAX_PHIL];

/* Philosopher i HUNGRY, хоёр хөрш нь идэхгүй байвал идэх эрх олгоно */
static void test(int i) {
    int l = (i + cfg.n - 1) % cfg.n, r = (i + 1) % cfg.n;
    if (mstate[i] == HUNGRY && mstate[l] != EATING && mstate[r] != EATING) {
        mstate[i] = EATING;
        log_state(i, EATING, i, (i + 1) % cfg.n, i);
        pthread_cond_signal(&self_cv[i]);
    }
}

/* Амжилттай бол 1, зогсоох дохио ирснээр цуцлагдвал 0 */
static int pickup_forks(int i) {
    pthread_mutex_lock(&mon);
    mstate[i] = HUNGRY;
    log_state(i, HUNGRY, -1, -1, 0);
    test(i);
    /* while: spurious wakeup болон зогсоох дохиог зөв боловсруулна */
    while (mstate[i] != EATING && !atomic_load(&stop_flag))
        pthread_cond_wait(&self_cv[i], &mon);
    int ok = (mstate[i] == EATING);
    if (!ok) mstate[i] = THINKING;
    pthread_mutex_unlock(&mon);
    return ok;
}

static void return_forks(int i) {
    pthread_mutex_lock(&mon);
    mstate[i] = THINKING;
    log_state(i, THINKING, i, (i + 1) % cfg.n, -1);
    test((i + cfg.n - 1) % cfg.n); /* зүүн хөрш хүлээж байсан бол сэрээнэ */
    test((i + 1) % cfg.n);         /* баруун хөрш */
    pthread_mutex_unlock(&mon);
}

static void *philo_monitor(void *arg) {
    int id = (int)(intptr_t)arg;
    unsigned s = cfg.seed * 1000003u + (unsigned)id * 7919u + 1u;
    while (!atomic_load(&stop_flag)) {
        sleep_ms(rnd(&s, cfg.think_min, cfg.think_max));
        if (atomic_load(&stop_flag) || !pickup_forks(id)) break;
        sleep_ms(rnd(&s, cfg.eat_min, cfg.eat_max));
        return_forks(id);
    }
    return NULL;
}

/* ---------- Алгоритм 2: naive (deadlock-той) ---------- */
static pthread_mutex_t fork_mx[MAX_PHIL];

/* Fork-ыг stop_flag-ийг шалгаж байж авна. Амжилттай бол 1 */
static int grab_fork(int k) {
    while (!atomic_load(&stop_flag)) {
        if (pthread_mutex_trylock(&fork_mx[k]) == 0) return 1;
        sleep_ms(1);
    }
    return 0;
}

static void *philo_naive(void *arg) {
    int id = (int)(intptr_t)arg, l = id, r = (id + 1) % cfg.n;
    unsigned s = cfg.seed * 1000003u + (unsigned)id * 7919u + 1u;
    while (!atomic_load(&stop_flag)) {
        sleep_ms(rnd(&s, cfg.think_min, cfg.think_max));
        if (atomic_load(&stop_flag)) break;
        log_state(id, HUNGRY, -1, -1, 0);
        if (!grab_fork(l)) break;
        log_state(id, HUNGRY, l, -1, id);       /* зүүн fork-той, хүлээж байна */
        sleep_ms(cfg.grab_delay_ms);            /* deadlock-д хүргэх хугацаа */
        if (!grab_fork(r)) {                    /* зогсоох дохио: зүүнийг тавина */
            log_state(id, HUNGRY, l, -1, -1);
            pthread_mutex_unlock(&fork_mx[l]);
            break;
        }
        log_state(id, EATING, -1, r, id);
        sleep_ms(rnd(&s, cfg.eat_min, cfg.eat_max));
        log_state(id, THINKING, l, r, -1);      /* эхлээд бүртгэж, дараа нь тавина */
        pthread_mutex_unlock(&fork_mx[r]);
        pthread_mutex_unlock(&fork_mx[l]);
    }
    return NULL;
}

/* Бүх philosopher зүүн fork-оо барьж, HUNGRY байвал deadlock */
static int is_deadlocked(void) {
    int dead = 1;
    pthread_mutex_lock(&ev_lock);
    for (int i = 0; i < cfg.n; i++)
        if (state[i] != HUNGRY || fork_owner[i] != i) dead = 0;
    pthread_mutex_unlock(&ev_lock);
    return dead;
}

/* ---------- Аргумент ---------- */
static void usage(FILE *f) {
    fprintf(f,
        "Хэрэглээ: dp [сонголт]\n"
        "  -n, --philosophers N   philosopher-ийн тоо, 2..%d (анхдагч 5)\n"
        "      --think-min MS     бодох хугацааны доод хязгаар (анхдагч 1000)\n"
        "      --think-max MS     бодох хугацааны дээд хязгаар (анхдагч 3000)\n"
        "      --eat-min MS       идэх хугацааны доод хязгаар (анхдагч 1000)\n"
        "      --eat-max MS       идэх хугацааны дээд хязгаар (анхдагч 3000)\n"
        "  -d, --duration MS      ажиллах хугацаа, 100..%d (анхдагч 10000)\n"
        "  -s, --seed N           санамсаргүй тооны seed (анхдагч 1)\n"
        "  -a, --algorithm NAME   monitor | naive (анхдагч monitor)\n"
        "      --grab-delay MS    naive: зүүн, баруун fork авах завсар (анхдагч 100)\n"
        "  -h, --help             энэ тусламж\n"
        "Гаралт: stdout руу JSON Lines. Гарах код: 0 хэвийн, 2 буруу аргумент, 3 deadlock.\n",
        MAX_PHIL, MAX_MS);
}

static int parse_int(const char *name, const char *s, long lo, long hi, int *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end != '\0') {
        fprintf(stderr, "алдаа: %s нь бүхэл тоо байх ёстой (өгсөн: '%s')\n", name, s);
        return -1;
    }
    if (v < lo || v > hi) {
        fprintf(stderr, "алдаа: %s нь %ld..%ld хооронд байх ёстой (өгсөн: %ld)\n",
                name, lo, hi, v);
        return -1;
    }
    *out = (int)v;
    return 0;
}

static int parse_args(int argc, char **argv) {
    cfg = (config_t){5, 1000, 3000, 1000, 3000, 10000, 100, 0, 1};
    static const struct option opts[] = {
        {"philosophers", required_argument, 0, 'n'},
        {"think-min", required_argument, 0, 1},
        {"think-max", required_argument, 0, 2},
        {"eat-min", required_argument, 0, 3},
        {"eat-max", required_argument, 0, 4},
        {"duration", required_argument, 0, 'd'},
        {"seed", required_argument, 0, 's'},
        {"algorithm", required_argument, 0, 'a'},
        {"grab-delay", required_argument, 0, 5},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};
    int c, v;
    while ((c = getopt_long(argc, argv, "n:d:s:a:h", opts, NULL)) != -1) {
        switch (c) {
        case 'n': if (parse_int("--philosophers", optarg, 2, MAX_PHIL, &cfg.n)) return -1; break;
        case 1: if (parse_int("--think-min", optarg, 0, MAX_MS, &cfg.think_min)) return -1; break;
        case 2: if (parse_int("--think-max", optarg, 0, MAX_MS, &cfg.think_max)) return -1; break;
        case 3: if (parse_int("--eat-min", optarg, 0, MAX_MS, &cfg.eat_min)) return -1; break;
        case 4: if (parse_int("--eat-max", optarg, 0, MAX_MS, &cfg.eat_max)) return -1; break;
        case 'd': if (parse_int("--duration", optarg, 100, MAX_MS, &cfg.duration_ms)) return -1; break;
        case 's': if (parse_int("--seed", optarg, 0, 2000000000, &v)) return -1; cfg.seed = (unsigned)v; break;
        case 5: if (parse_int("--grab-delay", optarg, 0, 10000, &cfg.grab_delay_ms)) return -1; break;
        case 'a':
            if (strcmp(optarg, "monitor") == 0) cfg.naive = 0;
            else if (strcmp(optarg, "naive") == 0) cfg.naive = 1;
            else { fprintf(stderr, "алдаа: --algorithm нь monitor эсвэл naive байх ёстой\n"); return -1; }
            break;
        case 'h': usage(stdout); exit(0);
        default: usage(stderr); return -1;
        }
    }
    if (optind < argc) { fprintf(stderr, "алдаа: үл мэдэгдэх аргумент '%s'\n", argv[optind]); return -1; }
    if (cfg.think_min > cfg.think_max) { fprintf(stderr, "алдаа: --think-min нь --think-max-аас их байж болохгүй\n"); return -1; }
    if (cfg.eat_min > cfg.eat_max) { fprintf(stderr, "алдаа: --eat-min нь --eat-max-аас их байж болохгүй\n"); return -1; }
    return 0;
}

/* ---------- main ---------- */
int main(int argc, char **argv) {
    if (parse_args(argc, argv) != 0) return 2;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    /* Энэ бичилт нь pthread_create()-аас (доор) ӨМНӨ, өөр ямар ч thread үүсээгүй
     * үед ганц root thread-ээс хийгддэг тул онолын хувьд өрсөлдөөнгүй (deterministic
     * happens-before pthread_create-ээр баталгаажна). Гэсэн ч бусад бүх stdout
     * бичилттэй ижил ev_lock-оор хамгаалж, glibc-ийн шинэ (__printf_buffer-суурьтай)
     * дотоод буфер хэрэгжилт дээр зарим динамик шинжилгээний хэрэгсэл (жишээ нь
     * Helgrind) "боломжит race" гэж дэмий сэрэмжлүүлэхээс бүрмөсөн зайлсхийхийн тулд
     * нэмэлт хамгаалалт болгов. */
    pthread_mutex_lock(&ev_lock);
    printf("{\"type\":\"config\",\"algorithm\":\"%s\",\"n\":%d,\"think_min\":%d,\"think_max\":%d,"
           "\"eat_min\":%d,\"eat_max\":%d,\"duration_ms\":%d,\"seed\":%u,\"grab_delay_ms\":%d}\n",
           cfg.naive ? "naive" : "monitor", cfg.n, cfg.think_min, cfg.think_max,
           cfg.eat_min, cfg.eat_max, cfg.duration_ms, cfg.seed, cfg.grab_delay_ms);
    fflush(stdout);
    pthread_mutex_unlock(&ev_lock);

    for (int i = 0; i < cfg.n; i++) {
        fork_owner[i] = -1;
        pthread_cond_init(&self_cv[i], NULL);
        pthread_mutex_init(&fork_mx[i], NULL);
    }
    for (int i = 0; i < cfg.n; i++) log_state(i, THINKING, -1, -1, 0); /* эхний төлөв */

    pthread_t th[MAX_PHIL];
    for (int i = 0; i < cfg.n; i++)
        if (pthread_create(&th[i], NULL, cfg.naive ? philo_naive : philo_monitor,
                           (void *)(intptr_t)i) != 0) {
            fprintf(stderr, "алдаа: thread үүсгэж чадсангүй\n");
            return 1;
        }

    int deadlock = 0;
    while (now_ms() < cfg.duration_ms) {
        struct timespec ts = {0, 10 * 1000000L};
        nanosleep(&ts, NULL);
        if (cfg.naive && is_deadlocked()) { deadlock = 1; break; }
    }
    if (deadlock) {
        /* Энд бусад philosopher thread-үүд ҮРГЭЛЖИЛСЭЭР (pthread_join хараахан
         * дуудагдаагүй) байгаа тул энэ бичилт нь log_state()-ийн бичилттэй бодитоор
         * зэрэгцэх боломжтой — ev_lock-оор заавал хамгаалах ёстой. */
        pthread_mutex_lock(&ev_lock);
        printf("{\"type\":\"deadlock\",\"t\":%ld}\n", now_ms());
        fflush(stdout);
        pthread_mutex_unlock(&ev_lock);
    }

    atomic_store(&stop_flag, 1);
    pthread_mutex_lock(&mon);                 /* cond_wait-д хүлээж буйг сэрээнэ */
    for (int i = 0; i < cfg.n; i++) pthread_cond_broadcast(&self_cv[i]);
    pthread_mutex_unlock(&mon);
    for (int i = 0; i < cfg.n; i++) pthread_join(th[i], NULL);

    /* Бүх thread аль хэдийн join хийгдсэн (дээр) тул state[]/hungry_since[]-д энд
     * mutex-гүйгээр уншихад аюулгүй. Хэрэв philosopher i ажиллагаа дуусах мөчид
     * HUNGRY (жишээ нь: deadlock эсвэл ноцтой starvation-ын улмаас нэг ч удаа
     * идэж чадаагүй) хэвээр байвал, түүний "эцэс хүртэл хүлээсэн" хугацааг тусад нь
     * гаргаж, мөн max_wait_ms-д оруулна — эс тэгвээс (зөвхөн ХОЛБОГДОХ eat event
     * дээр л шинэчлэгддэг тул) нэг ч удаа идээгүй philosopher "0.0 мс хүлээсэн" гэж
     * буруу харагдаж, starvation далдлагдах эрсдэлтэй (max_wait_ms нь "ажиглагдсан
     * хамгийн их хүлээлт" гэсэн утгатай тул одоо ч хүлээж байгаа philosopher-ийн
     * хугацааг хасаж орхих учиргүй). */
    long final_hungry_wait[MAX_PHIL];
    for (int i = 0; i < cfg.n; i++) {
        final_hungry_wait[i] = (state[i] == HUNGRY) ? (now_ms() - hungry_since[i]) : 0;
        if (final_hungry_wait[i] > wait_max[i]) wait_max[i] = final_hungry_wait[i];
    }

    printf("{\"type\":\"end\",\"t\":%ld,\"reason\":\"%s\",\"meals\":[", now_ms(),
           deadlock ? "deadlock" : "timeout");
    for (int i = 0; i < cfg.n; i++) printf("%ld%s", meals[i], i < cfg.n - 1 ? "," : "");
    printf("],\"avg_wait_ms\":[");
    for (int i = 0; i < cfg.n; i++)
        printf("%.1f%s", meals[i] ? (double)wait_total[i] / meals[i] : 0.0, i < cfg.n - 1 ? "," : "");
    printf("],\"max_wait_ms\":[");
    for (int i = 0; i < cfg.n; i++) printf("%ld%s", wait_max[i], i < cfg.n - 1 ? "," : "");
    printf("],\"final_hungry_wait_ms\":[");
    for (int i = 0; i < cfg.n; i++) printf("%ld%s", final_hungry_wait[i], i < cfg.n - 1 ? "," : "");
    printf("]}\n");
    fflush(stdout);
    return deadlock ? 3 : 0;
}
