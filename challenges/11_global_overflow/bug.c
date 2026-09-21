/*
 * Challenge 11 — Global Buffer Overflow (심화: 전역 아레나 bump 할당기)
 *
 * [시나리오]
 *   고정 크기 전역 버퍼(arena, .bss)를 "bump 포인터" 방식으로 나눠 쓰는 초간단
 *   할당기. 문자열 인터너(intern)가 들어온 문자열을 아레나에 복사해 보관한다.
 *
 * [기대 동작]
 *   문자열을 차례로 아레나에 인터닝하고, 마지막 문자열과 전체 길이 합을 출력한 뒤 정상 종료.
 *
 * [증상]
 *   arena_alloc() 이 남은 공간을 검사하지 않고 offset 만 증가시킨다(bump). 문자열을
 *   계속 인터닝하면 offset 이 ARENA_SIZE 를 넘어, 반환 포인터가 전역 배열 경계 밖을
 *   가리키게 되고 그 위치에 memcpy 로 쓰면서 .bss 를 벗어나 매핑되지 않은 영역까지
 *   침범 → SIGSEGV. 크래시는 intern 의 memcpy 에서 나지만, 원인은 "경계 미검사 bump".
 *
 * [gdb 로 잡기]
 *   make gdb NAME=11_global_overflow
 *   (gdb) run                          → 크래시(SIGSEGV)
 *   (gdb) bt                           → intern 의 memcpy 지점
 *   (gdb) print arena_off              → ARENA_SIZE 를 한참 초과했는지 확인
 *   (gdb) print (long)arena_off - (long)sizeof(arena)   → 경계에서 얼마나 넘었는지
 *   (gdb) print &arena[0]              → 반환 포인터가 arena 범위를 벗어났는지 대조
 *
 * [printf(로그)로 잡기]
 *   할당 때마다 offset 과 용량을 비교 출력:
 *     fprintf(stderr, "alloc n=%zu off=%zu cap=%zu\n", n, arena_off, sizeof(arena));
 *   → off 가 cap 을 넘어서도 계속 커지면 경계를 벗어난 것.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: arena_alloc() 에서 (arena_off + n <= sizeof(arena)) 를 반드시 검사하고,
 *       공간이 부족하면 NULL 반환 또는 오류 처리하세요.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_SIZE 4096
/* [Thinking Point]
 * 이 arena[] 는 함수 밖에 선언된 '전역 변수'다. 만약 이걸 arena_alloc() 함수 '안'의
 * 지역 변수로 옮기면 무슨 차이가 생길까?
 *   tip 1. 저장 위치가 다르다. 전역/static 은 프로그램 내내 사는 .bss/.data 영역에,
 *          지역 변수는 함수가 실행되는 동안만 사는 '스택'에 놓인다.
 *   tip 2. 수명이 다르다. 전역은 프로그램 시작~끝까지 유지되지만, 지역은 함수가 return
 *          하면 사라진다. → 그 주소를 함수 밖으로 돌려주면 7번(stack use-after-return)!
 *   tip 3. 초기화가 다르다. 전역/static 은 자동으로 0 으로 초기화되지만(그래서 .bss),
 *          지역 변수는 초기화하지 않으면 쓰레기 값이다(8번 챌린지).
 *   생각해보기: 여러 번 호출돼도 같은 저장소를 계속 나눠 쓰려면(커서 arena_off 유지)
 *               이 버퍼는 왜 전역(또는 static)이어야 할까? */
static unsigned char arena[ARENA_SIZE];    /* 전역(.bss) 아레나 */
static size_t arena_off = 0;

static void *arena_alloc(size_t n) {
    if (arena_off + n > sizeof(arena))
    {
        return NULL;
    }
        
    // arena_off의 초깃값은 0
    void *p = &arena[arena_off];
    // 마지막 널을 포함한 문자열의 길이만큼 더하기
    arena_off += n;

    // p는 문자열 시작을 가리키는 포인터
    return p;
}

static char *intern(const char *s) {
    // n -> 마지막 널을 포함한 문자열의 길이
    size_t n = strlen(s) + 1;
    // 문자열 시작 주소
    char *dst = arena_alloc(n);

    if (dst == NULL)
    {
        return NULL;
    }

    // memcpy 함수는 어떤 메모리 영역의 데이터든 지정한 바이트 크기만큼 다른 메모리 영역으로 빠르게 복사하는 표준 라이브러리 함수
    // frame 1
    // 기타 메모리 저장 공간 범위를 넘어가서 문제 발생
    memcpy(dst, s, n);                      /* 경계를 넘은 위치면 여기서 크래시 */
    // 복사한 문자열 시작 주소 리턴
    return dst;
}

int main(void) {
    
    const char *words[] = {
        "insert", "delete", "search", "traverse", "balance",
        "rotate", "rehash", "compact", "serialize", "checkpoint",
    };
    // nwords는 배열의 길이 -> 10
    int nwords = (int)(sizeof(words) / sizeof(words[0]));
    // last 인덱스를 찾는 건가?
    char *last = NULL;
    long total = 0;
    for (int i = 0; i < 100000; i++) {
        char buf[32];
        // snprintf 함수는 지정한 버퍼 크기만큼만 형식화된 문자열을 안전하게 저장하는 표준 함수
        // "words[i % 10]-i"
        // 문자열 최대 길이 30바이트인데... serialize-348
        snprintf(buf, sizeof buf, "%s-%d", words[i % nwords], i);
        // 저장한 문자열 복사 및 last에 저장
        // last = serialize-348에서 크래시
        last = intern(buf);              // frame 2

        if (last == NULL)
        {
            break;
        }

        // 총 문자열의 길이 구하기
        // total = 3761에서 크래시
        total += (long)strlen(last);
    }

    printf("interned, last=%s total_len=%ld\n", last, total);
    return 0;
}
