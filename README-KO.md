# Tutorial-OS 하드웨어 추상화 계층 (HAL)

베어메탈 하드웨어 상호작용을 통해 저수준 시스템 프로그래밍을 가르치기 위해 설계된 멀티 플랫폼 베어메탈 운영 체제입니다.

## 지원 플랫폼

| 보드                            | SoC             | 아키텍처     | 구현 상태    | 빌드 상태    |
|---------------------------------|-----------------|-------------|-------------|-------------|
| Raspberry Pi Zero 2W + GPi Case | BCM2710         | ARM          | ✅ 완료     | ✅ 통과     |
| Raspberry Pi 4B / CM4           | BCM2711         | ARM          | ✅ 완료     | ✅ 통과     |
| Raspberry Pi 5 / CM5            | BCM2712         | ARM          | ✅ 완료     | ✅ 통과     |
| Orange Pi RV 2                  | KYX1            | RISC-V       | ✅ 완료     | ✅ 통과     |
| LattePanda Iota                 | N150            | x86_64       | ❌ 미완료   | ❌ 실패     |
| LattePanda MU Compute           | N100            | x86_64       | ✅ 완료     | ✅ 통과     |
| Milk-V Mars                     | Starfive JH7110 | RISC-V       | ✅ 완료     | ✅ 통과     |

https://github.com/user-attachments/assets/3a25ab8a-6997-406c-826d-b38119a9d98b

## 디렉토리 구조

```
tutorial-os/
├── hal/                        # 하드웨어 추상화 계층 인터페이스
│   ├── hal.h                   # 마스터 포함 파일
│   ├── hal_types.h             # 타입, 에러 코드, MMIO
│   ├── hal_platform.h          # 플랫폼 정보, 온도, 클럭
│   ├── hal_timer.h             # 타이밍 및 딜레이
│   ├── hal_gpio.h              # GPIO 제어
│   └── hal_display.h           # 디스플레이 초기화
│
│   # 각 SoC는 동일한 파일 명명 패턴을 따름
├── soc/                        # SoC별 구현
│   ├── bcm2710/                # Raspberry Pi 3B, 3B+, 3A+, Zero 2 W, CM3 장치
│   │   ├── bcm2710_mailbox.h   # 메일박스 인터페이스
│   │   ├── bcm2710_regs.h      # 레지스터 정의
│   │   ├── boot_soc.S          # SoC별 부팅 코드
│   │   ├── display_dpi.c       # 디스플레이 구현 (DPI/HDMI)
│   │   ├── gpio.c              # GPIO 구현
│   │   ├── linker.ld           # 링커 스크립트
│   │   ├── mailbox.c           # 메일박스 구현
│   │   ├── soc.mk              # BCM2710 설정
│   │   ├── soc_init.c          # 플랫폼 초기화
│   │   └── timer.c             # 타이머 구현
│   ├── bcm2711/                # Raspberry Pi 4, CM4, Pi 400
│   │   ├── bcm2711_mailbox.h   # 메일박스 인터페이스
│   │   ├── bcm2711_regs.h      # 레지스터 정의
│   │   ├── boot_soc.S          # SoC별 부팅 코드
│   │   ├── display_dpi.c       # 디스플레이 구현 (DPI/HDMI)
│   │   ├── gpio.c              # GPIO 구현
│   │   ├── linker.ld           # 링커 스크립트
│   │   ├── mailbox.c           # 메일박스 구현
│   │   ├── soc.mk              # BCM2711 설정
│   │   ├── soc_init.c          # 플랫폼 초기화
│   │   └── timer.c             # 타이머 구현
│   ├── bcm2712/                # Raspberry Pi 5, CM5
│   │   ├── bcm2712_mailbox.h   # 메일박스 인터페이스
│   │   ├── bcm2712_regs.h      # 레지스터 정의
│   │   ├── boot_soc.S          # SoC별 부팅 코드
│   │   ├── display_dpi.c       # 디스플레이 구현 (DPI/HDMI)
│   │   ├── gpio.c              # GPIO 구현
│   │   ├── linker.ld           # 링커 스크립트
│   │   ├── mailbox.c           # 메일박스 구현
│   │   ├── soc.mk              # BCM2712 설정
│   │   ├── soc_init.c          # 플랫폼 초기화
│   │   └── timer.c             # 타이머 구현
│   ├── kyx1/                   # Orange Pi RV 2
│   │   ├── display_simplefb.c  # 디스플레이 드라이버
│   │   ├── blobs               # 빌드에서 추출한 U-Boot 바이너리 및 디바이스 트리 dts 파일
│   │   ├── drivers             # i2c, pmic_spm8821, sbi 드라이버 코드
│   │   ├── gpio.c              # GPIO 구현
│   │   ├── hal_platform_kyx1   # Pi에서 soc/bcm2710/soc_init.c가 하는 역할의 RISC-V 버전
│   │   ├── kyx1_cpu.h          # CPU 연산
│   │   ├── kyx1_regs.h         # 레지스터 정의
│   │   ├── linker.ld           # 링커 스크립트
│   │   ├── soc.mk              # KYX1 설정
│   │   ├── soc_init.c          # 플랫폼 초기화
│   │   ├── timer.c             # 타이머 구현
│   │   └── uart.c              # UART 드라이버
│   ├── lattepanda_n100/        # LattePanda MU용 N100 CPU
│   │   ├── display_gop.c       # 디스플레이 드라이버
│   │   ├── gpio.c              # GPIO 구현
│   │   ├── hal_platform_n100   # Pi에서 soc/bcm2710/soc_init.c가 하는 역할의 x86_64 버전
│   │   ├── linker.ld           # 링커 스크립트
│   │   ├── soc.mk              # N100 설정
│   │   ├── soc_init.c          # 플랫폼 초기화
│   │   ├── timer.c             # 타이머 구현
│   │   └── uart_8250.c         # UART 드라이버
│   ├── jh7110/                 # Milk-V Mars
│   │   ├── display_simplefb.c  # 디스플레이 드라이버
│   │   ├── blobs               # 디바이스 트리 dtb 파일
│   │   ├── gpio.c              # GPIO 구현
│   │   ├── hal_platform_jh7110 # Pi에서 soc/bcm2710/soc_init.c가 하는 역할의 RISC-V 버전
│   │   ├── jh7110_cpu.h        # CPU 연산
│   │   ├── jh7110_regs.h       # 레지스터 정의
│   │   ├── linker.ld           # 링커 스크립트
│   │   ├── soc.mk              # JH7110 설정
│   │   ├── mmu.S               # JH7110용 Sv39 페이지 테이블 설정
│   │   ├── soc_init.c          # 플랫폼 초기화
│   │   ├── timer.c             # 타이머 구현
│   └   └── uart.c              # UART 드라이버
│
├── board/                      # 보드별 설정
│   ├── rpi-zero2w-gpi/
│   │   ├── board.mk            # 빌드 설정
│   │   └── boot/               # SD 카드 부팅 파일
│   │       ├── config.txt      # VideoCore GPU 설정
│   │       └── BOOT_FILES.md   # 설명
│   │
│   ├── rpi-cm4-io/
│   │   ├── board.mk
│   │   └── boot/
│   │       ├── config.txt
│   │       └── BOOT_FILES.md
│   │
│   ├── milkv-mars/
│   │    ├── uEnv.txt
│   │    ├── board.mk
│   │    ├── DEPLOY.md
│   │    └── mkimage.sh          # U-Boot 설정이 포함된 이미지 생성
|   |
│   ├── lattepanda-mu/
│   │    ├── board.mk
│   │    ├── mkimage.py          # PE/COFF EFI 애플리케이션 설정이 포함된 이미지 생성
│   │    └── mkimage.sh          # mkimage.py의 .sh 래퍼 스크립트
│   │
│   └── orangepi-rv2/
│       ├── env_k1-x.txt
│       ├── board.mk
│       ├── boot.cmd
│       ├── DEPLOY.md
│       └── mkimage.sh          # U-Boot 설정이 포함된 이미지 생성
│
├── boot/                       # 핵심 어셈블리 진입점
│   ├── arm64/
│   │   ├── cache.S             # 캐시 유지보수 함수
│   │   ├── common_init.S       # 공통 SoC 후 초기화
│   │   ├── entry.S             # 진입점
│   │   └── vectors.S           # 예외 벡터 테이블
│   ├── riscv64/
│   │   ├── cache.S             # 캐시 유지보수 함수
│   │   ├── common_init.S       # 공통 SoC 후 초기화
│   │   ├── entry.S             # 진입점
│   │   └── vectors.S           # 예외 벡터 테이블
│   └── x86_64/                 # gnu-efi 사용으로 불필요하여 비어 있음
│
├── common/                     # 공유 최소 libc 및 MMIO
│   ├── mmio.h                  # 메모리 매핑 I/O 및 시스템 프리미티브
│   ├── string.c                # 메모리 및 문자열 함수
│   ├── string.h                # 문자열 및 메모리 함수 선언
│   └── types.h                 # 타입 정의
│
├── drivers/                    # 이식 가능 드라이버
│   ├── framebuffer/            # 드로잉 정의
│   │   ├── framebuffer.h       # 32비트 ARGB8888 프레임버퍼 드라이버
│   └   └── framebuffer.c       # 프레임버퍼 정의
│
├── kernel/                     # 커널 코드
│   └── main.c                  # 메인 애플리케이션 진입점
│
├── memory/                     # 메모리 관리
│   ├── allocator.h             # TLSF 기반 메모리 할당기 선언
│   └── allocator.c             # TLSF 기반 메모리 할당기
│
├── ui/                         # UI 시스템
│   ├── core/                   # 핵심 UI 캔버스 및 타입 정의
│   │   ├── ui_canvas.h         # 캔버스 및 텍스트 렌더러 인터페이스
│   │   └── ui_types.h          # 핵심 UI 타입 정의
│   ├── themes/                 # UI 테마 시스템
│   │   └── ui_theme.h          # UI 테마 시스템 정의
│   ├── widgets/                # 재사용 가능 UI 위젯 함수
│   │   ├── ui_widgets.h        # UI 위젯 정의
│   └   └── ui_widgets.c        # UI 위젯 구현
│
├── build.sh                    # Linux / MacOS에서 빌드
├── build.bat                   # Windows에서 빌드
├── docker-build.sh             # 빌드 시스템
├── Dockerfile                  # 빌드 시스템
├── Makefile                    # 빌드 시스템
└── README.md                   # 이 파일
```

## 빌드

```bash
# Raspberry Pi Zero 2W + GPi Case용 빌드
make LANG=c BOARD=rpi-zero2w-gpi

# Raspberry Pi CM4용 빌드
make LANG=rust BOARD=rpi-cm4-io

# 빌드 정보 표시
make info

# 정리
make clean

# 또는 Docker를 사용하여 빌드 스크립트로 전체 빌드
./build.bat    
./build.sh

# Milk-V Mars와 Orange Pi RV 2는 U-Boot 통합이 필요하므로 추가 빌드 단계가 있음
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=milkv-mars image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=orangepi-rv2 image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=lattepanda-mu image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=lattepanda-iota image
```

## 부팅 파일 (주의! 플랫폼별로 다름!)

각 보드는 서로 다른 부팅 요구사항이 있습니다. 자세한 내용은 `board/<이름>/boot/BOOT_FILES.md`를 참조하세요.

### Raspberry Pi (Zero 2W, CM4)

부팅 파티션에 필요한 파일:
```
/boot/
├── bootcode.bin      # (Pi Zero 2W만 해당, CM4는 불필요)
├── start.elf         # (CM4는 start4.elf 사용)
├── fixup.dat         # (CM4는 fixup4.dat 사용)
├── config.txt        # board/xxx/boot/에 제공됨
└── kernel8.img       # Tutorial-OS (빌드 출력물)
```

펌웨어 다운로드: https://github.com/raspberrypi/firmware/tree/master/boot

## 핵심 설계 원칙

### 1. 드로잉 코드의 이식성 유지

`fb_*()` 드로잉 함수는 플랫폼 간에 변경할 필요가 없습니다. 동일한 `main.c`가 ARM64, RISC-V64, x86_64에서 완전히 동일하게 렌더링됩니다:

```c
// 이 코드는 #ifdef 없이 모든 플랫폼에서 동작합니다
fb_clear(fb, 0xFF000000);
fb_fill_rect(fb, 10, 10, 100, 50, 0xFFFFFFFF);
fb_draw_string_transparent(fb, 20, 20, "Hello World!", 0xFFFFFFFF);
ui_draw_panel(fb, panel, &theme, UI_PANEL_ELEVATED);
```

### 2. HAL이 하드웨어 차이를 추상화

근본적으로 다른 세 가지 경로가 동일한 픽셀을 화면에 표시합니다 — HAL 덕분에 `main.c` 관점에서는 모두 동일합니다:

| 기능 | BCM2710/2711/2712 (ARM64) | JH7110 (RISC-V64) | x86_64 (UEFI) |
|------|--------------------------|-------------------|----------------|
| 부팅 | VideoCore GPU 펌웨어 | U-Boot + OpenSBI | UEFI 펌웨어 |
| 디스플레이 초기화 | 메일박스 프로퍼티 태그 | DTB의 SimpleFB | GOP 프로토콜 |
| 프레임버퍼 | VideoCore가 할당 | U-Boot가 사전 설정 | GOP가 할당 |
| 캐시 플러시 | ARM DSB + 캐시 연산 | SiFive L2 Flush64 | x86 CLFLUSH |
| 타이머 | MMIO 시스템 타이머 | RISC-V `rdtime` CSR | HPET / TSC |
| 플랫폼 정보 | 메일박스 쿼리 | 고정 상수 + DTB | CPUID + ACPI |

### 3. 컴파일 타임 플랫폼 선택

런타임에 `if (platform == X)` 검사를 하지 않습니다. 빌드 시스템이 컴파일 시점에 올바른 구현을 선택합니다:

```makefile
# board/milkv-mars/board.mk
SOC := jh7110
include soc/$(SOC)/soc.mk
```

### 4. 계약 우선 HAL 설계

HAL 인터페이스 헤더는 어떤 구현이 존재하기 전에 먼저 정의됩니다. 모든 플랫폼이 동일한 계약을 구현하므로, 드로잉 코드는 계약의 어느 쪽과 통신하는지 알 필요가 없습니다.

### 5. 에러 처리

HAL 함수는 `hal_error_t`를 반환합니다:

```c
hal_error_t err = hal_display_init(&fb);
if (HAL_FAILED(err)) {
    if (err == HAL_ERROR_DISPLAY_MAILBOX) { ... }
}
```

---

## 새 플랫폼 추가

1. **SoC 디렉토리 생성**: `soc/newsoc/`
2. **HAL 인터페이스 구현**:
    - `uart.c` — UART 드라이버 (디스플레이 작동 전 디버그 출력에 필요)
    - `timer.c` — 타이머 및 딜레이 함수
    - `gpio.c` — GPIO 제어
    - `soc_init.c` — 플랫폼 초기화
    - `display_*.c` — 디스플레이 드라이버
3. **레지스터 헤더 생성**: `newsoc_regs.h`
4. **빌드 규칙 생성**: `soc.mk`
5. **보드 설정 생성**: `board/newboard/board.mk`

**SimpleFB 기반 디스플레이를 위한 핵심 체크리스트** (U-Boot + 디바이스 트리 플랫폼):

`display_init`에서 `framebuffer_t`를 채운 후, 반환 전에 반드시 클립 스택을 초기화해야 합니다:

```c
fb->clip_depth      = 0;
fb->clip_stack[0].x = 0;
fb->clip_stack[0].y = 0;
fb->clip_stack[0].w = info.width;
fb->clip_stack[0].h = info.height;
fb->dirty_count     = 0;
fb->full_dirty      = false;
fb->frame_count     = 0;
fb->initialized     = true;
```

이 단계를 건너뛰면 모든 `fb_fill_rect`, `fb_draw_string` 및 위젯 호출이 아무것도 그리지 않지만, `fb_clear`는 계속 정상 작동합니다 — 디스플레이 파이프라인이 정상인 것처럼 보이지만 실제로는 그렇지 않습니다.

---

## 플랫폼별 참고 사항

### BCM2710 (Pi Zero 2W, Pi 3)
- 주변장치 베이스 주소: `0x3F000000`
- GPIO 풀업에는 GPPUD + GPPUDCLK 시퀀스 필요
- VideoCore 메일박스 프로퍼티 태그를 통한 디스플레이
- GPi Case용 GPIO 0–27의 DPI 출력 (ALT2)

### BCM2711 (Pi 4, CM4)
- 주변장치 베이스 주소: `0xFE000000`
- GPIO 풀업은 직접 2비트 레지스터 사용 (BCM2710보다 간단)
- BCM2710과 동일한 메일박스 인터페이스

### BCM2712 (Pi 5, CM5)
- RP1 사우스브릿지를 통한 주변장치 베이스 주소
- HDMI는 RP1을 통해 라우팅 — DPI GPIO 핀을 설정하지 마세요
- SET_DEPTH는 전체 할당 전에 별도의 메일박스 호출로 전송해야 함
- 반환된 pitch == width × 4 확인; pitch == width × 2는 16bpp 할당 실패를 의미

### JH7110 (Milk-V Mars)
- DRAM 베이스: `0x40000000`; 커널 로드 위치: `0x40200000`
- 프레임버퍼: `0xFE000000` (U-Boot `bdinfo`로 확인)
- 디스플레이 컨트롤러: DC8200, 주소 `0x29400000`
- L2 캐시 플러시는 `0x02010200`의 SiFive Flush64 사용 — `fence`만으로는 불충분
- U-Boot 2021.10은 `simple-framebuffer` DTB 노드를 주입**하지 않음** — 하드코딩된 폴백 경로는 이 U-Boot 버전에서 영구적이며, 임시 해결책이 아님
- CPU: SiFive U74-MC, RV64IMAFDCBX — Zicbom 없음, Svpbmt 없음

### x86_64 (LattePanda IOTA / MU)
- UEFI를 통한 부팅 — PE/COFF EFI 애플리케이션 위치: `\EFI\BOOT\BOOTX64.EFI`
- GOP (Graphics Output Protocol)를 통한 프레임버퍼 할당
- 디바이스 트리 없음 — CPUID 및 ACPI 테이블에서 플랫폼 정보 획득

---
교육 목적. LICENSE 파일을 참조하세요.