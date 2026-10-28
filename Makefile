CC      ?= gcc
CFLAGS  ?= -std=gnu11 -Wall -Wextra -O2
LDFLAGS += -pthread

.PHONY: all test clean tsan memcheck serve

all: build/dp

build/dp: src/dp.c
	@mkdir -p build
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# UI сервер (http://127.0.0.1:8000/)
serve: all
	python3 ui/server.py

# Автомат шалгалт (unit, интеграц, сервер, UI, ThreadSanitizer)
test: all
	bash tests/run_tests.sh

# ThreadSanitizer-тэй хувилбар (race condition шалгах)
tsan: src/dp.c
	@mkdir -p build
	$(CC) -std=gnu11 -Wall -Wextra -g -O1 -fsanitize=thread $< -o build/dp_tsan $(LDFLAGS)

# valgrind helgrind + leak-check (суулгасан байх шаардлагатай: sudo apt install
# valgrind). Анхаар: `make test`/run_tests.sh-ийн автомат 19 тестэд ОРОХГҮЙ,
# гар аргаар тусад нь ажиллуулдаг нэмэлт шалгалт. Monitor БА naive хоёуланд нь
# ажиллуулдаг (naive-ийг зориуд grab-delay>0-той, богино хугацаагаар туршина).
# --error-exitcode=99 заавал хэрэгтэй: naive тохиргоо нь санаатайгаар deadlock-д
# ордог тул ./build/dp ӨӨРӨӨ exit code 3-тай гардаг (энэ бол алдаа БИШ, хүлээгдэж
# буй ажиллагаа). Үүнийг valgrind-ийн ОЛСОН алдаанаас (race/leak) ялгахгүй бол
# make зүгээр л dp-ийн энгийн exit 3-ыг "memcheck унасан" гэж буруу тайлбарлана.
memcheck: all
	@fail=0; \
	for args in "-a monitor -n 5 --think-min 5 --think-max 20 --eat-min 5 --eat-max 20 -d 1500" \
	            "-a naive -n 5 --think-min 0 --think-max 0 --eat-min 300 --eat-max 600 --grab-delay 300 -d 1500"; do \
	  echo "-- helgrind: $$args"; \
	  valgrind --tool=helgrind --error-exitcode=99 -q ./build/dp $$args > /dev/null; rc=$$?; \
	  if [ $$rc -eq 99 ]; then echo "  FAIL: helgrind race илрүүлсэн"; fail=1; else echo "  OK"; fi; \
	  echo "-- leak-check: $$args"; \
	  valgrind --leak-check=full --error-exitcode=99 -q ./build/dp $$args > /dev/null; rc=$$?; \
	  if [ $$rc -eq 99 ]; then echo "  FAIL: санах ойн алдагдал илрүүлсэн"; fail=1; else echo "  OK"; fi; \
	done; \
	exit $$fail

clean:
	rm -rf build
