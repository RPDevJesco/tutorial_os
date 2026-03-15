# Tutorial-OS ハードウェア抽象化レイヤー（HAL）

ハードウェアへの直接操作を通じて、低レベルシステムプログラミングを学ぶためのマルチプラットフォーム・ベアメタルオペレーティングシステムです。

## 対応プラットフォーム

| ボード                          | SoC             | アーキテクチャ | 実装状態     | ビルド状態   |
|---------------------------------|-----------------|--------------|-------------|-------------|
| Raspberry Pi Zero 2W + GPi Case | BCM2710         | ARM          | ✅ 完了     | ✅ 成功     |
| Raspberry Pi 4B / CM4           | BCM2711         | ARM          | ✅ 完了     | ✅ 成功     |
| Raspberry Pi 5 / CM5            | BCM2712         | ARM          | ✅ 完了     | ✅ 成功     |
| Orange Pi RV 2                  | KYX1            | RISC-V       | ✅ 完了     | ✅ 成功     |
| LattePanda Iota                 | N150            | x86_64       | ❌ 未完了   | ❌ 失敗     |
| LattePanda MU Compute           | N100            | x86_64       | ✅ 完了     | ✅ 成功     |
| Milk-V Mars                     | Starfive JH7110 | RISC-V       | ✅ 完了     | ✅ 成功     |

https://github.com/user-attachments/assets/3a25ab8a-6997-406c-826d-b38119a9d98b

## ディレクトリ構造

```
tutorial-os/
├── hal/                        # ハードウェア抽象化レイヤーインターフェース
│   ├── hal.h                   # マスターインクルード
│   ├── hal_types.h             # 型、エラーコード、MMIO
│   ├── hal_platform.h          # プラットフォーム情報、温度、クロック
│   ├── hal_timer.h             # タイミングとディレイ
│   ├── hal_gpio.h              # GPIO 制御
│   └── hal_display.h           # ディスプレイ初期化
│
│   # 各 SoC は同一のファイル命名パターンに従う
├── soc/                        # SoC固有の実装
│   ├── bcm2710/                # Raspberry Pi 3B、3B+、3A+、Zero 2 W、CM3 デバイス
│   │   ├── bcm2710_mailbox.h   # メールボックスインターフェース
│   │   ├── bcm2710_regs.h      # レジスタ定義
│   │   ├── boot_soc.S          # SoC固有ブートコード
│   │   ├── display_dpi.c       # ディスプレイ実装（DPI/HDMI）
│   │   ├── gpio.c              # GPIO 実装
│   │   ├── linker.ld           # リンカスクリプト
│   │   ├── mailbox.c           # メールボックス実装
│   │   ├── soc.mk              # BCM2710 設定
│   │   ├── soc_init.c          # プラットフォーム初期化
│   │   └── timer.c             # タイマー実装
│   ├── bcm2711/                # Raspberry Pi 4、CM4、Pi 400
│   │   ├── bcm2711_mailbox.h   # メールボックスインターフェース
│   │   ├── bcm2711_regs.h      # レジスタ定義
│   │   ├── boot_soc.S          # SoC固有ブートコード
│   │   ├── display_dpi.c       # ディスプレイ実装（DPI/HDMI）
│   │   ├── gpio.c              # GPIO 実装
│   │   ├── linker.ld           # リンカスクリプト
│   │   ├── mailbox.c           # メールボックス実装
│   │   ├── soc.mk              # BCM2711 設定
│   │   ├── soc_init.c          # プラットフォーム初期化
│   │   └── timer.c             # タイマー実装
│   ├── bcm2712/                # Raspberry Pi 5、CM5
│   │   ├── bcm2712_mailbox.h   # メールボックスインターフェース
│   │   ├── bcm2712_regs.h      # レジスタ定義
│   │   ├── boot_soc.S          # SoC固有ブートコード
│   │   ├── display_dpi.c       # ディスプレイ実装（DPI/HDMI）
│   │   ├── gpio.c              # GPIO 実装
│   │   ├── linker.ld           # リンカスクリプト
│   │   ├── mailbox.c           # メールボックス実装
│   │   ├── soc.mk              # BCM2712 設定
│   │   ├── soc_init.c          # プラットフォーム初期化
│   │   └── timer.c             # タイマー実装
│   ├── kyx1/                   # Orange Pi RV 2
│   │   ├── display_simplefb.c  # ディスプレイドライバ
│   │   ├── blobs               # ビルドから抽出した U-Boot バイナリとデバイスツリー dts ファイル
│   │   ├── drivers             # i2c、pmic_spm8821、sbi ドライバコード
│   │   ├── gpio.c              # GPIO 実装
│   │   ├── hal_platform_kyx1   # Pi の soc/bcm2710/soc_init.c に相当する RISC-V 版
│   │   ├── kyx1_cpu.h          # CPU オペレーション
│   │   ├── kyx1_regs.h         # レジスタ定義
│   │   ├── linker.ld           # リンカスクリプト
│   │   ├── soc.mk              # KYX1 設定
│   │   ├── soc_init.c          # プラットフォーム初期化
│   │   ├── timer.c             # タイマー実装
│   │   └── uart.c              # UART ドライバ
│   ├── lattepanda_n100/        # LattePanda MU 用 N100 CPU
│   │   ├── display_gop.c       # ディスプレイドライバ
│   │   ├── gpio.c              # GPIO 実装
│   │   ├── hal_platform_n100   # Pi の soc/bcm2710/soc_init.c に相当する x86_64 版
│   │   ├── linker.ld           # リンカスクリプト
│   │   ├── soc.mk              # N100 設定
│   │   ├── soc_init.c          # プラットフォーム初期化
│   │   ├── timer.c             # タイマー実装
│   │   └── uart_8250.c         # UART ドライバ
│   ├── jh7110/                 # Milk-V Mars
│   │   ├── display_simplefb.c  # ディスプレイドライバ
│   │   ├── blobs               # デバイスツリー dtb ファイル
│   │   ├── gpio.c              # GPIO 実装
│   │   ├── hal_platform_jh7110 # Pi の soc/bcm2710/soc_init.c に相当する RISC-V 版
│   │   ├── jh7110_cpu.h        # CPU オペレーション
│   │   ├── jh7110_regs.h       # レジスタ定義
│   │   ├── linker.ld           # リンカスクリプト
│   │   ├── soc.mk              # JH7110 設定
│   │   ├── mmu.S               # JH7110 用 Sv39 ページテーブルセットアップ
│   │   ├── soc_init.c          # プラットフォーム初期化
│   │   ├── timer.c             # タイマー実装
│   └   └── uart.c              # UART ドライバ
│
├── board/                      # ボード固有の設定
│   ├── rpi-zero2w-gpi/
│   │   ├── board.mk            # ビルド設定
│   │   └── boot/               # SDカード用ブートファイル
│   │       ├── config.txt      # VideoCore GPU 設定
│   │       └── BOOT_FILES.md   # 説明
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
│   │    └── mkimage.sh          # U-Boot 設定を含むイメージを作成
|   |
│   ├── lattepanda-mu/
│   │    ├── board.mk
│   │    ├── mkimage.py          # PE/COFF EFI アプリケーション設定を含むイメージを作成
│   │    └── mkimage.sh          # mkimage.py の .sh ラッパースクリプト
│   │
│   └── orangepi-rv2/
│       ├── env_k1-x.txt
│       ├── board.mk
│       ├── boot.cmd
│       ├── DEPLOY.md
│       └── mkimage.sh          # U-Boot 設定を含むイメージを作成
│
├── boot/                       # コアアセンブリエントリポイント
│   ├── arm64/
│   │   ├── cache.S             # キャッシュメンテナンス関数
│   │   ├── common_init.S       # 共通 SoC 後初期化
│   │   ├── entry.S             # エントリポイント
│   │   └── vectors.S           # 例外ベクタテーブル
│   ├── riscv64/
│   │   ├── cache.S             # キャッシュメンテナンス関数
│   │   ├── common_init.S       # 共通 SoC 後初期化
│   │   ├── entry.S             # エントリポイント
│   │   └── vectors.S           # 例外ベクタテーブル
│   └── x86_64/                 # gnu-efi 使用のため不要（空）
│
├── common/                     # 共有の最小 libc と MMIO
│   ├── mmio.h                  # メモリマップド I/O とシステムプリミティブ
│   ├── string.c                # メモリおよび文字列関数
│   ├── string.h                # 文字列およびメモリ関数宣言
│   └── types.h                 # 型定義
│
├── drivers/                    # ポータブルドライバ
│   ├── framebuffer/            # 描画定義
│   │   ├── framebuffer.h       # 32ビット ARGB8888 フレームバッファドライバ
│   └   └── framebuffer.c       # フレームバッファ定義
│
├── kernel/                     # カーネルコード
│   └── main.c                  # メインアプリケーションエントリポイント
│
├── memory/                     # メモリ管理
│   ├── allocator.h             # TLSF ベースメモリアロケータ宣言
│   └── allocator.c             # TLSF ベースメモリアロケータ
│
├── ui/                         # UI システム
│   ├── core/                   # コア UI キャンバスおよび型定義
│   │   ├── ui_canvas.h         # キャンバスおよびテキストレンダラインターフェース
│   │   └── ui_types.h          # コア UI 型定義
│   ├── themes/                 # UI テーマシステム
│   │   └── ui_theme.h          # UI テーマシステム定義
│   ├── widgets/                # 再利用可能な UI ウィジェット関数
│   │   ├── ui_widgets.h        # UI ウィジェット定義
│   └   └── ui_widgets.c        # UI ウィジェット実装
│
├── build.sh                    # Linux / MacOS でのビルド
├── build.bat                   # Windows でのビルド
├── docker-build.sh             # ビルドシステム
├── Dockerfile                  # ビルドシステム
├── Makefile                    # ビルドシステム
└── README.md                   # このファイル
```

## ビルド

```bash
# Raspberry Pi Zero 2W + GPi Case 用にビルド
make LANG=c BOARD=rpi-zero2w-gpi

# Raspberry Pi CM4 用にビルド
make LANG=rust BOARD=rpi-cm4-io

# ビルド情報を表示
make info

# クリーン
make clean

# または Docker を使用してビルドスクリプトで一括ビルド
./build.bat    
./build.sh

# Milk-V Mars と Orange Pi RV 2 は U-Boot 統合が必要なため、追加のビルド手順があります
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=milkv-mars image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=orangepi-rv2 image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=lattepanda-mu image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=lattepanda-iota image
```

## ブートファイル（注意！プラットフォーム固有！）

各ボードには異なるブート要件があります。詳細は `board/<名前>/boot/BOOT_FILES.md` を参照してください。

### Raspberry Pi（Zero 2W、CM4）

ブートパーティションに必要なファイル：
```
/boot/
├── bootcode.bin      # （Pi Zero 2W のみ、CM4 は不要）
├── start.elf         # （CM4 は start4.elf を使用）
├── fixup.dat         # （CM4 は fixup4.dat を使用）
├── config.txt        # board/xxx/boot/ に同梱
└── kernel8.img       # Tutorial-OS（ビルド出力）
```

ファームウェアの入手先：https://github.com/raspberrypi/firmware/tree/master/boot

## 設計の基本原則

### 1. 描画コードのポータビリティ維持

`fb_*()` 描画関数はプラットフォーム間で変更不要です。同じ `main.c` が ARM64、RISC-V64、x86_64 で完全に同一の描画結果を生成します：

```c
// このコードは #ifdef なしで全プラットフォームで動作します
fb_clear(fb, 0xFF000000);
fb_fill_rect(fb, 10, 10, 100, 50, 0xFFFFFFFF);
fb_draw_string_transparent(fb, 20, 20, "Hello World!", 0xFFFFFFFF);
ui_draw_panel(fb, panel, &theme, UI_PANEL_ELEVATED);
```

### 2. HAL によるハードウェア差異の吸収

根本的に異なる3つの経路で同じピクセルを画面に表示 — HAL により `main.c` の視点からはすべて同一に見えます：

| 機能 | BCM2710/2711/2712 (ARM64) | JH7110 (RISC-V64) | x86_64 (UEFI) |
|------|--------------------------|-------------------|----------------|
| ブート | VideoCore GPU ファームウェア | U-Boot + OpenSBI | UEFI ファームウェア |
| ディスプレイ初期化 | メールボックスプロパティタグ | DTB の SimpleFB | GOP プロトコル |
| フレームバッファ | VideoCore が割り当て | U-Boot が事前設定 | GOP が割り当て |
| キャッシュフラッシュ | ARM DSB + キャッシュ操作 | SiFive L2 Flush64 | x86 CLFLUSH |
| タイマー | MMIO システムタイマー | RISC-V `rdtime` CSR | HPET / TSC |
| プラットフォーム情報 | メールボックスクエリ | 固定定数 + DTB | CPUID + ACPI |

### 3. コンパイル時のプラットフォーム選択

ランタイムでの `if (platform == X)` チェックはありません。ビルドシステムがコンパイル時に正しい実装を選択します：

```makefile
# board/milkv-mars/board.mk
SOC := jh7110
include soc/$(SOC)/soc.mk
```

### 4. コントラクトファーストの HAL 設計

HAL インターフェースヘッダは、いかなる実装が存在する前に定義されます。すべてのプラットフォームが同一のコントラクトを実装するため、描画コードはコントラクトのどちら側と通信しているかを知る必要がありません。

### 5. エラー処理

HAL 関数は `hal_error_t` を返します：

```c
hal_error_t err = hal_display_init(&fb);
if (HAL_FAILED(err)) {
    if (err == HAL_ERROR_DISPLAY_MAILBOX) { ... }
}
```

---

## 新しいプラットフォームの追加

1. **SoC ディレクトリを作成**：`soc/newsoc/`
2. **HAL インターフェースを実装**：
    - `uart.c` — UART ドライバ（ディスプレイ動作前のデバッグ出力に必要）
    - `timer.c` — タイマーおよびディレイ関数
    - `gpio.c` — GPIO 制御
    - `soc_init.c` — プラットフォーム初期化
    - `display_*.c` — ディスプレイドライバ
3. **レジスタヘッダを作成**：`newsoc_regs.h`
4. **ビルドルールを作成**：`soc.mk`
5. **ボード設定を作成**：`board/newboard/board.mk`

**SimpleFB ベースディスプレイのための重要チェックリスト**（U-Boot + デバイスツリープラットフォーム）：

`display_init` で `framebuffer_t` を設定した後、リターン前に必ずクリップスタックを初期化してください：

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

この手順を省略すると、すべての `fb_fill_rect`、`fb_draw_string`、ウィジェット呼び出しが何も描画しないまま、`fb_clear` だけが正常に動作し続けます — ディスプレイパイプラインが正常に見えて実際には正常でない状態になります。

---

## プラットフォーム固有の注意事項

### BCM2710（Pi Zero 2W、Pi 3）
- ペリフェラルベースアドレス：`0x3F000000`
- GPIO プルアップには GPPUD + GPPUDCLK シーケンスが必要
- VideoCore メールボックスプロパティタグによるディスプレイ
- GPi Case 用に GPIO 0–27 の DPI 出力（ALT2）

### BCM2711（Pi 4、CM4）
- ペリフェラルベースアドレス：`0xFE000000`
- GPIO プルアップは直接2ビットレジスタ（BCM2710 より簡単）
- BCM2710 と同じメールボックスインターフェース

### BCM2712（Pi 5、CM5）
- RP1 サウスブリッジ経由のペリフェラルベースアドレス
- HDMI は RP1 経由でルーティング — DPI GPIO ピンを設定しないでください
- SET_DEPTH は完全な割り当て前に別のメールボックス呼び出しで送信する必要あり
- 戻り値の pitch == width × 4 を確認；pitch == width × 2 は 16bpp 割り当て失敗を意味

### JH7110（Milk-V Mars）
- DRAM ベース：`0x40000000`；カーネルロード位置：`0x40200000`
- フレームバッファ：`0xFE000000`（U-Boot `bdinfo` で確認済み）
- ディスプレイコントローラ：DC8200、アドレス `0x29400000`
- L2 キャッシュフラッシュは `0x02010200` の SiFive Flush64 を使用 — `fence` のみでは不十分
- U-Boot 2021.10 は `simple-framebuffer` DTB ノードを注入**しない** — ハードコードされたフォールバックパスはこの U-Boot バージョンでは恒久的であり、一時的な回避策ではない
- CPU：SiFive U74-MC、RV64IMAFDCBX — Zicbom なし、Svpbmt なし

### x86_64（LattePanda IOTA / MU）
- UEFI 経由でブート — PE/COFF EFI アプリケーション配置先：`\EFI\BOOT\BOOTX64.EFI`
- GOP（Graphics Output Protocol）経由でフレームバッファを割り当て
- デバイスツリーなし — プラットフォーム情報は CPUID および ACPI テーブルから取得

---
教育目的。LICENSE ファイルを参照してください。