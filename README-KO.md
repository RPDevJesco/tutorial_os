# Tutorial-OS 하드웨어 추상화 레이어(HAL)

여러 아키텍처에 걸쳐 실제 하드웨어를 대상으로 하는 베어메탈 교육용 운영체제.
본 문서는 Tutorial-OS의 Rust 패리티 구현으로, C 구현과의 **설계 원칙 패리티**를 목표로 합니다.
코드 행 단위의 구조적 복제가 아닙니다.

## 설계 철학

C와 Rust 두 구현은 동일한 아키텍처 개념을 공유합니다——계층화된 HAL, SoC별 구현, 공유 포터블 드라이버——
다만 각 언어의 고유한 관용구를 통해 표현됩니다.
C는 Makefile 캐스케이드(`board.mk → soc.mk → Makefile`)로 계층적 분리를 구현하고,
Rust는 Cargo 워크스페이스와 의존성 해석을 통해 컴파일 시점에 동일한 경계를 구현합니다.
두 구현 간의 구조적 차이 자체가 교육적 의미를 가집니다:
두 언어가 근본적으로 다른 도구로 동일한 시스템 문제를 해결하는 것입니다.

**패리티의 의미:**
- 동일한 HAL 컨트랙트(C의 함수 포인터 테이블 대신 Rust trait으로 표현)
- 동일한 하드웨어 지원(보드 및 SoC 커버리지 완전 일치)
- 동일한 부트 흐름(어셈블리 엔트리 포인트는 공유하며 재구현하지 않음)
- 동일한 UI 및 디스플레이 출력(Hardware Inspector 렌더링 결과 동일)

**패리티가 의미하지 않는 것:**
- 동일한 파일명이나 디렉토리 깊이
- 코드 행 수나 함수 시그니처 일치
- C의 패턴을 Rust에 강제하거나 그 반대

## 지원 플랫폼

| 보드                             | SoC             | 아키텍처      | 구현 상태      | 빌드 상태     | C 코드 상태   | Rust 코드 상태  |
|---------------------------------|-----------------|--------------|---------------|-------------|--------------|-----------------|
| Raspberry Pi Zero 2W + GPi Case | BCM2710         | ARM          | ✅ 완료        | ✅ 통과     | ✅ 완료      | ✅ 완료          |
| Raspberry Pi 4B / CM4           | BCM2711         | ARM          | ✅ 완료        | ✅ 통과     | ✅ 완료      | ❌ 미완료        |
| Raspberry Pi 5 / CM5            | BCM2712         | ARM          | ✅ 완료        | ✅ 통과     | ✅ 완료      | ❌ 미완료        |
| Orange Pi RV 2                  | KYX1            | RISC-V       | ✅ 완료        | ✅ 통과     | ✅ 완료      | ❌ 미완료        |
| LattePanda Iota                 | N150            | x86_64       | ✅ 완료        | ✅ 통과     | ✅ 완료      | ❌ 미완료        |
| LattePanda MU Compute           | N100 / N305     | x86_64       | ✅ 완료        | ✅ 통과     | ✅ 완료      | ❌ 미완료        |
| Milk-V Mars                     | Starfive JH7110 | RISC-V       | ✅ 완료        | ✅ 통과     | ✅ 완료      | ✅ 완료          |

## C와 Rust 구현 비교

| 관점 | C 구현 | Rust 구현 |
|------|--------|----------|
| 빌드 시스템 | `board.mk → soc.mk → Makefile` | Cargo 워크스페이스 + feature flags |
| HAL 컨트랙트 | 함수 포인터 테이블(`hal_platform_t`) | Trait(`pub trait Platform`) |
| 경계 강제 | 관례(개발자 자율에 의존) | 컴파일 시점 보장(crate 의존 관계) |
| SoC 선택 | Makefile include 체인 | `--features board-xxx` |
| 어셈블리 통합 | Makefile에서 직접 참조 | `build.rs` + `cc` crate |
| 외부 의존성 | 없음(프리스탠딩 C) | 런타임 의존 없음(제로 crate) |
| 링커 스크립트 | 공유 | 공유(동일 파일) |
| 부트 어셈블리 | 공유 | 공유(동일 파일) |


https://github.com/user-attachments/assets/3a25ab8a-6997-406c-826d-b38119a9d98b

## 디렉토리 구조

```
tutorial-os/
├── hal/src/                    # 하드웨어 추상화 레이어 인터페이스
│   ├── hal.h                   # 마스터 인클루드
│   ├── hal_types.h             # 타입, 에러 코드, MMIO
│   ├── hal_cpu.h               # CPU 연산
│   ├── hal_platform.h          # 플랫폼 정보, 온도, 클럭
│   ├── hal_timer.h             # 타이밍 및 지연
│   ├── hal_gpio.h              # GPIO 제어
│   ├── hal_dsi.h               # 포터블 DSI/DCS 커맨드 레이어
│   ├── hal_dma.h               # 캐시 일관성, 주소 변환, 버퍼 소유권 추적
│   ├── lib.rs                  # 공유 라이브러리
│   ├── cpu.rs                  # CPU 연산
│   ├── display.rs              # 디스플레이 초기화
│   ├── dma.rs                  # 캐시 일관성, 주소 변환, 버퍼 소유권 추적
│   ├── dsi.rs                  # 포터블 DSI/DCS 커맨드 레이어
│   ├── gpio.rs                 # GPIO 제어
│   ├── timer.rs                # 타이밍 및 지연
│   ├── types.rs                # 타입, 에러 코드, MMIO
│   └── hal_display.h           # 디스플레이 초기화
│
│   # 각 SoC는 동일한 파일 패턴을 따르는 것을 목표로 함
├── soc                                 # SoC 고유 구현
│   ├── bcm2710                         # Raspberry Pi 3B, 3B+, 3A+, Zero 2 W, CM3 디바이스
│   │   ├── boot_soc.S                  # SoC 고유 부트 코드
│   │   ├── build.rs                    # 공유 ARM64 부트 어셈블리 컴파일
│   │   ├── Cargo.toml                  # bcm2710 Crate
│   │   ├── linker.ld                   # 링커 스크립트
│   │   ├── soc.mk                      # bcm2710 빌드 설정
│   │   ├── /src/  
│   │   │   ├── bcm2710_mailbox.h       # Mailbox 인터페이스
│   │   │   ├── bcm2710_regs.h          # 레지스터 정의
│   │   │   ├── display_dpi.c           # 디스플레이 구현(DPI/HDMI)
│   │   │   ├── gpio.c                  # GPIO 구현
│   │   │   ├── mailbox.c               # Mailbox 구현
│   │   │   ├── mailbox.rs              # Mailbox 구현
│   │   │   ├── regs.rs                 # 레지스터 정의
│   │   │   ├── soc_init.c              # 플랫폼 초기화
│   │   │   ├── soc_init.rs             # 플랫폼 초기화
│   │   │   ├── timer.c                 # 타이머 구현
│   │   │   └── timer.rs                # 타이머 구현

│   ├── jh7110/                         # Milk-V Mars
│   │   ├── blobs                       # 디바이스 트리 DTB 파일
│   │   ├── build.rs                    # 공유 RISC-V 부트 어셈블리 컴파일
│   │   ├── Cargo.toml                  # jh7110 Crate
│   │   ├── linker.ld                   # 링커 스크립트
│   │   ├── mmu.S                       # JH7110용 Sv39 페이지 테이블 설정
│   │   ├── soc.mk                      # jh7110 빌드 설정
│   │   ├── /src/    
│   │   │   ├── /drivers/   
│   │   │   │   ├── mod.rs              # 공유 라이브러리
│   │   │   │   ├── i2c.c               # Synopsys DesignWare I2C 마스터 드라이버
│   │   │   │   ├── i2c.h               # Synopsys DesignWare I2C 마스터 드라이버
│   │   │   │   ├── i2c.rs              # Synopsys DesignWare I2C 마스터 드라이버
│   │   │   │   ├── pmic_aaxp15060.c    # X-Powers AXP15060 PMIC 드라이버
│   │   │   │   ├── pmic_aaxp15060.h    # X-Powers AXP15060 PMIC 드라이버
│   │   │   │   ├── pmic_aaxp15060.rs   # X-Powers AXP15060 PMIC 드라이버
│   │   │   │   ├── sbi.c               # SBI(Supervisor Binary Interface) ecall 인터페이스
│   │   │   │   ├── sbi.h               # SBI(Supervisor Binary Interface) ecall 인터페이스
│   │   │   │   └── sbi.rs              # SBI(Supervisor Binary Interface) ecall 인터페이스
│   │   │   ├── cache.c                 # 캐시 관리
│   │   │   ├── cache.rs                # 캐시 관리
│   │   │   ├── cpu.rs                  # CPU 연산
│   │   │   ├── display_simplefb.c      # 디스플레이 드라이버
│   │   │   ├── display_simplefb.rs     # 디스플레이 드라이버
│   │   │   ├── gpio.c                  # GPIO 구현
│   │   │   ├── jh7110_cpu.h            # CPU 연산
│   │   │   ├── lib.rs                  # 공유 라이브러리
│   │   │   ├── gpio.rs                 # GPIO 구현
│   │   │   ├── hal_platform_jh7110.c   # Pi에서 soc/bcm2710/soc_init.c에 해당하는 RISC-V 버전
│   │   │   ├── jh7110_regs.h           # 레지스터 정의
│   │   │   ├── soc_init.c              # 플랫폼 초기화
│   │   │   ├── soc_init.rs             # 플랫폼 초기화
│   │   │   ├── timer.c                 # 타이머 구현
│   │   │   ├── timer.rs                # 타이머 구현
│   │   │   ├── uart.c                  # UART 드라이버
│   │   │   └── uart.rs                 # UART 드라이버
│
├── board/                      # 보드 고유 설정
│   ├── rpi-zero2w-gpi/
│   │   ├── board.mk            # 빌드 설정
│   │   └── boot/               # SD 카드 부트 파일
│   │       ├── config.txt      # VideoCore GPU 설정
│   │       └── BOOT_FILES.md   # 안내 문서
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
│   │    └── mkimage.sh          # U-Boot 설정을 포함한 이미지 생성
|   |
│   ├── lattepanda-mu/
│   │    ├── board.mk
│   │    ├── mkimage.py          # PE/COFF EFI 애플리케이션 설정을 포함한 이미지 생성
│   │    └── mkimage.sh          # mkimage.py의 .sh 래퍼
│   │
│   └── orangepi-rv2/
│       ├── env_k1-x.txt
│       ├── board.mk
│       ├── boot.cmd
│       ├── DEPLOY.md
│       └── mkimage.sh          # U-Boot 설정을 포함한 이미지 생성
│
├── boot/                       # 코어 어셈블리 엔트리 포인트
│   ├── arm64/
│   │   ├── cache.S             # 캐시 유지보수 함수
│   │   ├── common_init.S       # 공통 SoC 후 초기화
│   │   ├── entry.S             # 엔트리 포인트
│   │   └── vectors.S           # 예외 벡터 테이블
│   ├── riscv64/
│   │   ├── cache.S             # 캐시 유지보수 함수
│   │   ├── common_init.S       # 공통 SoC 후 초기화
│   │   ├── entry.S             # 엔트리 포인트
│   │   └── vectors.S           # 예외 벡터 테이블
│   └── x86_64/                 # 빈 디렉토리, gnu-efi에서는 불필요
│
├── common/src/                 # 공유 최소 libc 서브셋 및 MMIO
│   ├── lib.rs                  # 공유 라이브러리
│   ├── mem.rs                  # 컴파일러 필수 메모리 내장 함수
│   ├── mmio.rs                 # 메모리 매핑 I/O 및 시스템 프리미티브
│   ├── mmio.h                  # 메모리 매핑 I/O 및 시스템 프리미티브
│   ├── string.c                # 메모리 및 문자열 함수
│   ├── string.h                # 문자열 및 메모리 함수 선언
│   ├── types.rs                # 타입 정의
│   └── types.h                 # 타입 정의
│
├── drivers/src/                # 포터블 드라이버
│   ├── framebuffer/            # 드로잉 정의
│   │   ├── fb_pixel.h          # 포맷 인식 픽셀 접근 헬퍼
│   │   ├── mod.rs              # 32비트 ARGB8888 프레임버퍼 드라이버 및 포맷 인식 픽셀 접근 헬퍼
│   │   ├── framebuffer.h       # 32비트 ARGB8888 프레임버퍼 드라이버
│   └   └── framebuffer.c       # 프레임버퍼 정의
│
├── kernel/src/                 # 커널 코드
│   ├── main.rs                 # 메인 애플리케이션 엔트리 포인트
│   └── main.c                  # 메인 애플리케이션 엔트리 포인트
│
├── memory/src/                 # 메모리 관리
│   ├── lib.rs                  # 공유 라이브러리
│   ├── allocator.rs            # TLSF 기반 메모리 할당자 선언
│   ├── allocator.h             # TLSF 기반 메모리 할당자 선언
│   └── allocator.c             # TLSF 기반 메모리 할당자
│
├── ui/                         # UI 시스템
│   ├── core/src/               # 코어 UI 캔버스 및 타입 정의
│   │   ├── mod.rs              # 공유 라이브러리
│   │   ├── types.rs            # 코어 UI 타입 정의
│   │   ├── canvas.rs           # 캔버스 및 텍스트 렌더러 인터페이스
│   │   ├── ui_canvas.h         # 캔버스 및 텍스트 렌더러 인터페이스
│   │   └── ui_types.h          # 코어 UI 타입 정의
│   ├── themes/src/             # UI 테마 시스템
│   │   ├── mod.rs              # 공유 라이브러리
│   │   ├── theme.rs            # UI 테마 시스템 정의
│   │   └── ui_theme.h          # UI 테마 시스템 정의
│   ├── widgets/src/            # 재사용 가능한 UI 위젯 함수
│   │   ├── mod.rs              # 공유 라이브러리
│   │   ├── widgets.rs          # UI 위젯 정의
│   │   ├── ui_widgets.h        # UI 위젯 정의
│   └   └── ui_widgets.c        # UI 위젯 구현
│
├── build.sh                    # Linux / MacOS 빌드 스크립트
├── build.bat                   # Windows 빌드 스크립트
├── cargo.toml                  # Rust 빌드 시스템
├── build.bat                   # Windows 빌드 스크립트
├── docker-build.sh             # 빌드 시스템
├── Dockerfile                  # 빌드 시스템
├── Makefile                    # 빌드 시스템
└── README.md                   # 본 파일
```

## 빌드

```bash
# 지정 보드용 빌드. .bat과 .sh는 동일한 인수를 사용.
# C 또는 Rust를 선택하여 빌드 가능. 기본값은 C이며, rust 언어 파라미터를 추가하면 Rust로 빌드됩니다.
build.bat rpi-zero2w-gpi      :: → output/rpi-zero2w/kernel8.img
build.bat rpi-cm4 rust        :: → output/rpi-cm4/kernel8.img
build.bat rpi-5               :: → output/rpi-5/kernel8.img
build.bat orangepi-rv2        :: → output/orangepi-rv2/kernel.bin
build.bat milkv-mars          :: → output/milkv-mars/kernel.bin
build.bat lattepanda-mu       :: → output/lattepanda-mu/kernel.efi
build.bat lattepanda-iota     :: → output/lattepanda-iota/kernel.efi
build.bat clean               :: target/ 및 output/ 삭제

# Milk-V Mars와 Orange Pi RV 2는 U-Boot 통합을 위한 추가 빌드 단계가 필요
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=milkv-mars image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=orangepi-rv2 image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=lattepanda-mu image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=lattepanda-iota image
```

## 부트 파일(주의! 플랫폼별로 다름!)

각 보드의 부트 요구사항이 다릅니다. 상세 내용은 `board/<n>/boot/BOOT_FILES.md`를 참조하세요.

### Raspberry Pi(Zero 2W, CM4)

부트 파티션 필요 파일:
```
/boot/
├── bootcode.bin      # (Pi Zero 2W만 해당, CM4는 불필요)
├── start.elf         # (CM4는 start4.elf)
├── fixup.dat         # (CM4는 fixup4.dat)
├── config.txt        # board/xxx/boot/에 제공됨
└── kernel8.img       # Tutorial-OS(빌드 출력)
```

펌웨어 다운로드: https://github.com/raspberrypi/firmware/tree/master/boot

## 핵심 설계 원칙

### 1. HAL이 하드웨어 차이를 추상화

화면의 동일한 하나의 픽셀에 도달하는 근본적으로 다른 3가지 경로——HAL이 이들을 `main.c`의 관점에서 완전히 동일하게 만듭니다:

| 기능 | BCM2710/2711/2712(ARM64) | JH7110(RISC-V64) | x86_64(UEFI) |
|------|--------------------------|-------------------|---------------|
| 부트 | VideoCore GPU 펌웨어 | U-Boot + OpenSBI | UEFI 펌웨어 |
| 디스플레이 초기화 | Mailbox 프로퍼티 태그 | DTB의 SimpleFB | GOP 프로토콜 |
| 프레임버퍼 | VideoCore가 할당 | U-Boot이 사전 설정 | GOP가 할당 |
| 캐시 플러시 | ARM DSB + 캐시 연산 | SiFive L2 Flush64 | x86 CLFLUSH |
| 타이머 | MMIO 시스템 타이머 | RISC-V `rdtime` CSR | HPET / TSC |
| 플랫폼 정보 | Mailbox 쿼리 | 고정 상수 + DTB | CPUID + ACPI |

### 2. 컴파일 시점 플랫폼 선택

런타임 `if (platform == X)` 검사는 일절 없음. 빌드 시스템이 컴파일 시점에 올바른 구현을 선택합니다:

```makefile
# board/milkv-mars/board.mk
SOC := jh7110
include soc/$(SOC)/soc.mk
```

### 3. 컨트랙트 우선 HAL 설계

HAL 인터페이스 헤더는 어떠한 구현이 존재하기 전에 먼저 정의됩니다. 모든 플랫폼이 동일한 컨트랙트를 구현하며,
드로잉 코드는 컨트랙트의 어느 쪽과 통신하는지 알 필요가 없습니다.

---

## 플랫폼별 참고사항

### BCM2710(Pi Zero 2W, Pi 3)
- 페리페럴 베이스 주소: `0x3F000000`
- GPIO 풀업에는 GPPUD + GPPUDCLK 시퀀스 필요
- VideoCore Mailbox 프로퍼티 태그를 통한 디스플레이
- GPi Case용 GPIO 0–27(ALT2)에서 DPI 출력

### BCM2711(Pi 4, CM4)
- 페리페럴 베이스 주소: `0xFE000000`
- GPIO 풀업은 직접 2비트 레지스터로 구현(BCM2710보다 단순)
- BCM2710과 동일한 Mailbox 인터페이스

### BCM2712(Pi 5, CM5)
- 페리페럴 베이스 주소는 RP1 사우스브릿지 경유
- HDMI는 RP1을 통해 라우팅——DPI GPIO 핀을 설정하지 **말 것**
- SET_DEPTH는 전체 할당 전에 별도의 Mailbox 호출로 전송해야 함
- 반환된 pitch == width × 4 인지 확인할 것. pitch == width × 2이면 16bpp 할당 실패를 의미

### JH7110(Milk-V Mars)
- DRAM 베이스 주소: `0x40000000`; 커널 로드 주소: `0x40200000`
- 프레임버퍼: `0xFE000000`(U-Boot `bdinfo`로 확인 완료)
- 디스플레이 컨트롤러: DC8200(`0x29400000`)
- L2 캐시 플러시는 SiFive Flush64(`0x02010200`) 경유——`fence`만으로는 불충분
- U-Boot 2021.10은 `simple-framebuffer` DTB 노드를 주입하지 **않음**——하드코딩된 폴백 경로는 이 U-Boot 버전에서 영구적이며, 임시 우회책이 아님
- CPU: SiFive U74-MC, RV64IMAFDCBX——Zicbom 없음, Svpbmt 없음

### x86_64(LattePanda IOTA / MU)
- UEFI를 통해 부트——PE/COFF EFI 애플리케이션(`\EFI\BOOT\BOOTX64.EFI`)
- 프레임버퍼는 GOP(Graphics Output Protocol)를 통해 할당
- 디바이스 트리 없음——플랫폼 정보는 CPUID 및 ACPI 테이블에서 취득

---
교육 목적. LICENSE 파일을 참조하세요.