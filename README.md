# Dining-Philosophers Problem — БД1 (II-4-11)

**Хичээл:** F.CSM302 Үйлдлийн систем — Бие даалт 1
**Сэдэв:** 11. The Dining-Philosophers Problem (*Operating System Concepts*, 7-р бүлэг — Synchronization Examples)
**Баг:** II-4-11

## Зорилго

Сурах бичгийн Dining-Philosophers Programming Project-ийг C/pthread дээр хэрэгжүүлж,
**thinking → hungry → eating** төлөв шилжилтийг, fork (нөөц) эзэмшилтийг, болон хоёр
алгоритмын (deadlock-гүй **monitor** шийдэл, deadlock-той **naive** шийдэл) ялгааг
browser-based интерактив UI (state diagram) дээр дүрсэлнэ.

## Бүтэц

```
├── src/dp.c          # Үндсэн C/pthread хэрэгжилт (monitor + naive)
├── Makefile          # all / test / tsan / memcheck / serve / clean
├── ui/
│   ├── index.html    # Интерактив state-diagram UI
│   ├── model.js       # DOM-гүй логик (ui/node хоёуланд ашиглагдана)
│   └── server.py       # localhost-д ажилладаг NDJSON streaming сервер
├── tests/
│   ├── check_log.py    # dp-ийн гаралтын инвариант шалгагч
│   ├── test_model.js    # model.js-ийн unit тест (Node)
│   ├── run_tests.sh      # бүх автомат шалгалтыг ажиллуулагч (`make test`)
│   └── examples/          # жишээ гаралт (.jsonl) лог файлууд
└── docs/
    ├── Report.pdf / Report.docx     # Эцсийн тайлан (заавал PDF)
    ├── Manual.pdf / Manual.docx     # Эцсийн Manual (заавал PDF)
    └── figures/                     # Тайланд ашигласан бодит screenshot-ууд
```

> `build/` хавтас эх кодоос `make all`-аар үүсдэг тул repo-д хадгалагдаагүй (`.gitignore`).

## Quick Start

```bash
# 1) Build
make all

# 2) Автомат шалгалт (unit + интеграц + сервер + ThreadSanitizer)
make test

# 3) Тушаалын мөрөөс шууд ажиллуулах
./build/dp -a monitor -n 5 --think-min 1000 --think-max 3000 \
           --eat-min 1000 --eat-max 3000 -d 10000 -s 1

# 4) Интерактив UI (localhost:8000)
make serve
# → хөтөчөөр http://127.0.0.1:8000/ нээх
```

## Хэрэглэсэн хэл, сан, орчин

- **C (gnu11)**, POSIX threads (`pthread`) — цэвэр стандарт сан, гадны dependency байхгүй.
- **Python 3 (стандарт сан)** — localhost UI сервер (`http.server`), шалгалтын скрипт.
- **Vanilla JavaScript** — UI логик, гадны framework ашигласангүй.
- AI ашиглалт: Claude (Anthropic)-г код бичих, тест бэлтгэх, тайлан/manual боловсруулахад
  ашигласан. Бүх үр дүнг `make test`, `valgrind helgrind/leak-check`, `ThreadSanitizer`-ээр
  бодитоор баталгаажуулсан (доорх "Тест" хэсгийг үз).

## Тест

`make test` нь дараах шалгалтуудыг дарааллаар ажиллуулна:

1. Compile (`-Wall -Wextra`, алдаа/сануулгагүй)
2. `tests/test_model.js` — UI логикийн unit тест (Node)
3. `monitor` алгоритм — тухайн ажиллагаанд инвариант зөрчигдөөгүй, бүгд дор хаяж 3 удаа идсэн (n=5) — **starvation-гүй гэдгийг БАТАЛГААЖУУЛААГҮЙ**, зөвхөн энэ богино шалгалтад ажиглагдаагүй (доор "Known issues" үзнэ үү)
4. `naive` алгоритм — тохирсон (random бус think-хугацаа + grab-delay>0) тохиргоонд deadlock давтагдана (гарах код 3)
5. Захын тохиолдол: `n=2` (хамгийн бага), `n=64` (хамгийн их)
6. Буруу аргумент (`-n 1`, `think-min > think-max`) зөв алдаа өгч байгаа эсэх, мөн UI серверийн буруу бүтэцтэй тоон утга (жишээ нь `n=--5`) зөв 400 алдаа өгч, сервер амьд үлдэх эсэх
7. UI серверийн `/health`, `/`, `/run`, буруу параметрийн шалгалт
8. **ThreadSanitizer** (`monitor` БА `naive` хоёуланд нь) — race condition тухайн ажиллагаанд илрээгүй

Нийт 16 автомат шалгалт. Нэмэлтээр (`make test`-д ОРОХГҮЙ, гар аргаар ажиллуулдаг):
`make memcheck` → valgrind **helgrind** (race) болон **leak-check** (санах ойн
алдагдал), `monitor` ба `naive` хоёуланд нь — дөрвөн команд, давтан туршилтад
цэвэр.

## Known issues

- `naive` алгоритм нь тохирсон тохиргоонд **санаатайгаар** deadlock-д ордог
  (сурах бичгийн "naive" жишээг судлах зорилготой) — энэ алдаа биш, сайтар
  тодорхойлогдсон (grab-delay>0, think тогтмол) тохиргоонд детерминист
  ажиллагаа. Харин `grab-delay=0` үед эсрэгээр — deadlock биш, ноцтой
  starvation ажиглагддаг (`docs/Report.docx`, 9-р хэсэг).
- `monitor` алгоритм нь сурах бичгийн дагуу deadlock-гүй ч **starvation-аас
  бүрэн хамгаалдаггүй** (онолын хувьд боломжтой) — `make test`-ийн богино
  шалгалт үүнийг "баталгаажуулдаггүй", зөвхөн тухайн ажиллагаанд ажиглагдаагүйг
  л харуулдаг.
- UI сервер зөвхөн `127.0.0.1`-д сонсдог (аюулгүй байдлын үүднээс гадаад сүлжээнээс
  хандах боломжгүй).
