# Tutorial-OS ハードウェア抽象化レイヤー（HAL）

複数アーキテクチャにわたり、実ハードウェアを対象としたベアメタル教育用オペレーティングシステム。
本ドキュメントは Tutorial-OS の Rust パリティ実装であり、C 実装との**設計原則のパリティ**を目指しています。
行単位の構造的な複製ではありません。

## 設計思想

C と Rust の両実装は同じアーキテクチャ概念を共有しています——階層化された HAL、SoC ごとの実装、共有されたポータブルドライバ——
ただし、それぞれの言語のネイティブなイディオムで表現されます。
C では Makefile のカスケード（`board.mk → soc.mk → Makefile`）で階層的な分離を実現し、
Rust では Cargo ワークスペースと依存関係解決によりコンパイル時に同じ境界を実現します。
両実装間の構造的な相違そのものが教育的な意味を持ちます：
2つの言語が根本的に異なるツールで同じシステム問題を解決しているのです。

**パリティの意味：**
- 同一の HAL コントラクト（C の関数ポインタテーブルの代わりに Rust の trait で表現）
- 同一のハードウェアサポート（ボードと SoC のカバレッジは完全一致）
- 同一のブートフロー（アセンブリのエントリポイントは共有され、再実装しない）
- 同一の UI およびディスプレイ出力（Hardware Inspector のレンダリング結果は同一）

**パリティが意味しないもの：**
- 同一のファイル名やディレクトリ階層
- 行数や関数シグネチャの一致
- C のパターンを Rust に強制する、またはその逆

## 対応プラットフォーム

| ボード                           | SoC             | アーキテクチャ | 実装状態       | ビルド状態    | C コード状態   | Rust コード状態  |
|---------------------------------|-----------------|--------------|---------------|-------------|--------------|-----------------|
| Raspberry Pi Zero 2W + GPi Case | BCM2710         | ARM          | ✅ 完了        | ✅ パス     | ✅ 完了      | ✅ 完了          |
| Raspberry Pi 4B / CM4           | BCM2711         | ARM          | ✅ 完了        | ✅ パス     | ✅ 完了      | ❌ 未完了        |
| Raspberry Pi 5 / CM5            | BCM2712         | ARM          | ✅ 完了        | ✅ パス     | ✅ 完了      | ❌ 未完了        |
| Orange Pi RV 2                  | KYX1            | RISC-V       | ✅ 完了        | ✅ パス     | ✅ 完了      | ❌ 未完了        |
| LattePanda Iota                 | N150            | x86_64       | ✅ 完了        | ✅ パス     | ✅ 完了      | ❌ 未完了        |
| LattePanda MU Compute           | N100 / N305     | x86_64       | ✅ 完了        | ✅ パス     | ✅ 完了      | ❌ 未完了        |
| Milk-V Mars                     | Starfive JH7110 | RISC-V       | ✅ 完了        | ✅ パス     | ✅ 完了      | ✅ 完了          |

## C と Rust の実装比較

| 観点 | C 実装 | Rust 実装 |
|------|--------|----------|
| ビルドシステム | `board.mk → soc.mk → Makefile` | Cargo ワークスペース + feature flags |
| HAL コントラクト | 関数ポインタテーブル（`hal_platform_t`） | Trait（`pub trait Platform`） |
| 境界の強制 | 慣習（開発者の自律に依存） | コンパイル時保証（crate 依存関係） |
| SoC 選択 | Makefile include チェーン | `--features board-xxx` |
| アセンブリ統合 | Makefile 内で直接参照 | `build.rs` + `cc` crate |
| 外部依存関係 | なし（フリースタンディング C） | ランタイム依存なし（ゼロ crate） |
| リンカスクリプト | 共有 | 共有（同一ファイル） |
| ブートアセンブリ | 共有 | 共有（同一ファイル） |


https://github.com/user-attachments/assets/3a25ab8a-6997-406c-826d-b38119a9d98b

## ディレクトリ構造

```
tutorial-os/
├── hal/src/                    # ハードウェア抽象化レイヤーインターフェース
│   ├── hal.h                   # マスターインクルード
│   ├── hal_types.h             # 型、エラーコード、MMIO
│   ├── hal_cpu.h               # CPU 操作
│   ├── hal_platform.h          # プラットフォーム情報、温度、クロック
│   ├── hal_timer.h             # タイミングと遅延
│   ├── hal_gpio.h              # GPIO 制御
│   ├── hal_dsi.h               # ポータブル DSI/DCS コマンドレイヤー
│   ├── hal_dma.h               # キャッシュコヒーレンシ、アドレス変換、バッファ所有権追跡
│   ├── lib.rs                  # 共有ライブラリ
│   ├── cpu.rs                  # CPU 操作
│   ├── display.rs              # ディスプレイ初期化
│   ├── dma.rs                  # キャッシュコヒーレンシ、アドレス変換、バッファ所有権追跡
│   ├── dsi.rs                  # ポータブル DSI/DCS コマンドレイヤー
│   ├── gpio.rs                 # GPIO 制御
│   ├── timer.rs                # タイミングと遅延
│   ├── types.rs                # 型、エラーコード、MMIO
│   └── hal_display.h           # ディスプレイ初期化
│
│   # 各 SoC は同一のファイルパターンに従うことを目指す
├── soc                                 # SoC 固有の実装
│   ├── bcm2710                         # Raspberry Pi 3B、3B+、3A+、Zero 2 W、CM3 デバイス
│   │   ├── boot_soc.S                  # SoC 固有ブートコード
│   │   ├── build.rs                    # 共有 ARM64 ブートアセンブリのコンパイル
│   │   ├── Cargo.toml                  # bcm2710 Crate
│   │   ├── linker.ld                   # リンカスクリプト
│   │   ├── soc.mk                      # bcm2710 ビルド設定
│   │   ├── /src/  
│   │   │   ├── bcm2710_mailbox.h       # Mailbox インターフェース
│   │   │   ├── bcm2710_regs.h          # レジスタ定義
│   │   │   ├── display_dpi.c           # ディスプレイ実装（DPI/HDMI）
│   │   │   ├── gpio.c                  # GPIO 実装
│   │   │   ├── mailbox.c               # Mailbox 実装
│   │   │   ├── mailbox.rs              # Mailbox 実装
│   │   │   ├── regs.rs                 # レジスタ定義
│   │   │   ├── soc_init.c              # プラットフォーム初期化
│   │   │   ├── soc_init.rs             # プラットフォーム初期化
│   │   │   ├── timer.c                 # タイマー実装
│   │   │   └── timer.rs                # タイマー実装

│   ├── jh7110/                         # Milk-V Mars
│   │   ├── blobs                       # デバイスツリー DTB ファイル
│   │   ├── build.rs                    # 共有 RISC-V ブートアセンブリのコンパイル
│   │   ├── Cargo.toml                  # jh7110 Crate
│   │   ├── linker.ld                   # リンカスクリプト
│   │   ├── mmu.S                       # JH7110 用 Sv39 ページテーブル設定
│   │   ├── soc.mk                      # jh7110 ビルド設定
│   │   ├── /src/    
│   │   │   ├── /drivers/   
│   │   │   │   ├── mod.rs              # 共有ライブラリ
│   │   │   │   ├── i2c.c               # Synopsys DesignWare I2C マスタードライバ
│   │   │   │   ├── i2c.h               # Synopsys DesignWare I2C マスタードライバ
│   │   │   │   ├── i2c.rs              # Synopsys DesignWare I2C マスタードライバ
│   │   │   │   ├── pmic_aaxp15060.c    # X-Powers AXP15060 PMIC ドライバ
│   │   │   │   ├── pmic_aaxp15060.h    # X-Powers AXP15060 PMIC ドライバ
│   │   │   │   ├── pmic_aaxp15060.rs   # X-Powers AXP15060 PMIC ドライバ
│   │   │   │   ├── sbi.c               # SBI（Supervisor Binary Interface）ecall インターフェース
│   │   │   │   ├── sbi.h               # SBI（Supervisor Binary Interface）ecall インターフェース
│   │   │   │   └── sbi.rs              # SBI（Supervisor Binary Interface）ecall インターフェース
│   │   │   ├── cache.c                 # キャッシュ管理
│   │   │   ├── cache.rs                # キャッシュ管理
│   │   │   ├── cpu.rs                  # CPU 操作
│   │   │   ├── display_simplefb.c      # ディスプレイドライバ
│   │   │   ├── display_simplefb.rs     # ディスプレイドライバ
│   │   │   ├── gpio.c                  # GPIO 実装
│   │   │   ├── jh7110_cpu.h            # CPU 操作
│   │   │   ├── lib.rs                  # 共有ライブラリ
│   │   │   ├── gpio.rs                 # GPIO 実装
│   │   │   ├── hal_platform_jh7110.c   # Pi における soc/bcm2710/soc_init.c に相当する RISC-V 版
│   │   │   ├── jh7110_regs.h           # レジスタ定義
│   │   │   ├── soc_init.c              # プラットフォーム初期化
│   │   │   ├── soc_init.rs             # プラットフォーム初期化
│   │   │   ├── timer.c                 # タイマー実装
│   │   │   ├── timer.rs                # タイマー実装
│   │   │   ├── uart.c                  # UART ドライバ
│   │   │   └── uart.rs                 # UART ドライバ
│
├── board/                      # ボード固有の設定
│   ├── rpi-zero2w-gpi/
│   │   ├── board.mk            # ビルド設定
│   │   └── boot/               # SD カードブートファイル
│   │       ├── config.txt      # VideoCore GPU 設定
│   │       └── BOOT_FILES.md   # 手順書
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
│   │    └── mkimage.sh          # mkimage.py の .sh ラッパー
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
│   └── x86_64/                 # 空ディレクトリ、gnu-efi では不要
│
├── common/src/                 # 共有の最小限 libc サブセットと MMIO
│   ├── lib.rs                  # 共有ライブラリ
│   ├── mem.rs                  # コンパイラ必須メモリ組込み関数
│   ├── mmio.rs                 # メモリマップド I/O とシステムプリミティブ
│   ├── mmio.h                  # メモリマップド I/O とシステムプリミティブ
│   ├── string.c                # メモリおよび文字列関数
│   ├── string.h                # 文字列およびメモリ関数宣言
│   ├── types.rs                # 型定義
│   └── types.h                 # 型定義
│
├── drivers/src/                # ポータブルドライバ
│   ├── framebuffer/            # 描画定義
│   │   ├── fb_pixel.h          # フォーマット対応ピクセルアクセスヘルパー
│   │   ├── mod.rs              # 32 ビット ARGB8888 フレームバッファドライバおよびフォーマット対応ピクセルアクセスヘルパー
│   │   ├── framebuffer.h       # 32 ビット ARGB8888 フレームバッファドライバ
│   └   └── framebuffer.c       # フレームバッファ定義
│
├── kernel/src/                 # カーネルコード
│   ├── main.rs                 # メインアプリケーションエントリポイント
│   └── main.c                  # メインアプリケーションエントリポイント
│
├── memory/src/                 # メモリ管理
│   ├── lib.rs                  # 共有ライブラリ
│   ├── allocator.rs            # TLSF ベースメモリアロケータ宣言
│   ├── allocator.h             # TLSF ベースメモリアロケータ宣言
│   └── allocator.c             # TLSF ベースメモリアロケータ
│
├── ui/                         # UI システム
│   ├── core/src/               # コア UI キャンバスと型定義
│   │   ├── mod.rs              # 共有ライブラリ
│   │   ├── types.rs            # コア UI 型定義
│   │   ├── canvas.rs           # キャンバスおよびテキストレンダラインターフェース
│   │   ├── ui_canvas.h         # キャンバスおよびテキストレンダラインターフェース
│   │   └── ui_types.h          # コア UI 型定義
│   ├── themes/src/             # UI テーマシステム
│   │   ├── mod.rs              # 共有ライブラリ
│   │   ├── theme.rs            # UI テーマシステム定義
│   │   └── ui_theme.h          # UI テーマシステム定義
│   ├── widgets/src/            # 再利用可能な UI ウィジェット関数
│   │   ├── mod.rs              # 共有ライブラリ
│   │   ├── widgets.rs          # UI ウィジェット定義
│   │   ├── ui_widgets.h        # UI ウィジェット定義
│   └   └── ui_widgets.c        # UI ウィジェット実装
│
├── build.sh                    # Linux / MacOS ビルドスクリプト
├── build.bat                   # Windows ビルドスクリプト
├── cargo.toml                  # Rust ビルドシステム
├── build.bat                   # Windows ビルドスクリプト
├── docker-build.sh             # ビルドシステム
├── Dockerfile                  # ビルドシステム
├── Makefile                    # ビルドシステム
└── README.md                   # 本ファイル
```

## ビルド

```bash
# 指定ボード向けにビルド。.bat と .sh は同じ引数を使用。
# C または Rust を選択してビルド可能。デフォルトは C、rust パラメータを追加すると Rust でビルドされます。
build.bat rpi-zero2w-gpi      :: → output/rpi-zero2w/kernel8.img
build.bat rpi-cm4 rust        :: → output/rpi-cm4/kernel8.img
build.bat rpi-5               :: → output/rpi-5/kernel8.img
build.bat orangepi-rv2        :: → output/orangepi-rv2/kernel.bin
build.bat milkv-mars          :: → output/milkv-mars/kernel.bin
build.bat lattepanda-mu       :: → output/lattepanda-mu/kernel.efi
build.bat lattepanda-iota     :: → output/lattepanda-iota/kernel.efi
build.bat clean               :: target/ と output/ を削除

# Milk-V Mars と Orange Pi RV 2 は U-Boot 統合のため追加のビルド手順が必要
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=milkv-mars image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=orangepi-rv2 image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=lattepanda-mu image
docker run --rm -v ${PWD}:/src -w /src --entrypoint make tutorial-os-builder BOARD=lattepanda-iota image
```

## ブートファイル（注意！プラットフォーム固有！）

各ボードのブート要件は異なります。詳細は `board/<name>/boot/BOOT_FILES.md` を参照してください。

### Raspberry Pi（Zero 2W、CM4）

ブートパーティションに必要なファイル：
```
/boot/
├── bootcode.bin      # （Pi Zero 2W のみ、CM4 では不要）
├── start.elf         # （CM4 では start4.elf）
├── fixup.dat         # （CM4 では fixup4.dat）
├── config.txt        # board/xxx/boot/ に同梱
└── kernel8.img       # Tutorial-OS（ビルド出力）
```

ファームウェアの入手先：https://github.com/raspberrypi/firmware/tree/master/boot

## 主要な設計原則

### 1. HAL がハードウェアの差異を抽象化

画面上の同じ1ピクセルに至る3つの根本的に異なるパス——HAL がそれらを `main.c` の視点から完全に同一に見せます：

| 機能 | BCM2710/2711/2712（ARM64） | JH7110（RISC-V64） | x86_64（UEFI） |
|------|---------------------------|-------------------|----------------|
| ブート | VideoCore GPU ファームウェア | U-Boot + OpenSBI | UEFI ファームウェア |
| ディスプレイ初期化 | Mailbox プロパティタグ | DTB からの SimpleFB | GOP プロトコル |
| フレームバッファ | VideoCore が割り当て | U-Boot が事前設定 | GOP が割り当て |
| キャッシュフラッシュ | ARM DSB + キャッシュ操作 | SiFive L2 Flush64 | x86 CLFLUSH |
| タイマー | MMIO システムタイマー | RISC-V `rdtime` CSR | HPET / TSC |
| プラットフォーム情報 | Mailbox クエリ | 固定定数 + DTB | CPUID + ACPI |

### 2. コンパイル時プラットフォーム選択

ランタイムの `if (platform == X)` チェックは一切なし。ビルドシステムがコンパイル時に正しい実装を選択します：

```makefile
# board/milkv-mars/board.mk
SOC := jh7110
include soc/$(SOC)/soc.mk
```

### 3. コントラクトファースト HAL 設計

HAL インターフェースヘッダは、いかなる実装が存在する前に定義されます。すべてのプラットフォームが同じコントラクトを実装し、
描画コードはコントラクトのどちら側と通信しているかを知る必要がありません。

---

## プラットフォーム固有の注意事項

### BCM2710（Pi Zero 2W、Pi 3）
- ペリフェラルベースアドレス：`0x3F000000`
- GPIO プルアップには GPPUD + GPPUDCLK シーケンスが必要
- VideoCore Mailbox プロパティタグによるディスプレイ
- GPi Case 用に GPIO 0–27（ALT2）で DPI 出力

### BCM2711（Pi 4、CM4）
- ペリフェラルベースアドレス：`0xFE000000`
- GPIO プルアップは直接 2 ビットレジスタで実装（BCM2710 より簡素）
- BCM2710 と同じ Mailbox インターフェース

### BCM2712（Pi 5、CM5）
- ペリフェラルベースアドレスは RP1 サウスブリッジ経由
- HDMI は RP1 経由でルーティング——DPI GPIO ピンを設定**しないこと**
- SET_DEPTH は完全な割り当ての前に、別の Mailbox 呼び出しで送信する必要あり
- 返却された pitch == width × 4 を確認すること。pitch == width × 2 の場合は 16bpp 割り当てに失敗している

### JH7110（Milk-V Mars）
- DRAM ベースアドレス：`0x40000000`、カーネルロードアドレス：`0x40200000`
- フレームバッファ：`0xFE000000`（U-Boot `bdinfo` で確認済み）
- ディスプレイコントローラ：DC8200（`0x29400000`）
- L2 キャッシュフラッシュは SiFive Flush64（`0x02010200`）経由——`fence` 単体では不十分
- U-Boot 2021.10 は `simple-framebuffer` DTB ノードを注入**しない**——ハードコードされたフォールバックパスはこの U-Boot バージョンでは恒久的であり、一時的な回避策ではない
- CPU：SiFive U74-MC、RV64IMAFDCBX——Zicbom なし、Svpbmt なし

### x86_64（LattePanda IOTA / MU）
- UEFI 経由でブート——PE/COFF EFI アプリケーション（`\EFI\BOOT\BOOTX64.EFI`）
- フレームバッファは GOP（Graphics Output Protocol）経由で割り当て
- デバイスツリーなし——プラットフォーム情報は CPUID および ACPI テーブルから取得

---
教育用途。LICENSE ファイルを参照してください。