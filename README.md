# Mbed on TOPPERS ASP3

MbedアプリケーションをITRON仕様のリアルタイムカーネルであるTOPPERS ASP3上で手軽に動作させる実装、あるいはTOPPERS ASP3をMbedに対応したターゲットに手早く移植する実装です。

## ビルド方法

Mbed CLI 2 (mbed-tools) を必要とします。Mbed CLI 2のインストール方法は公式のドキュメントを参照ください。

sample1は以下の手順でビルドできます。

```shell
$ mbed-tools import https://github.com/komori-t/mbed-asp3 # 時間がかかります
$ cd mbed-asp3/asp3/
$ mkdir obj # このディレクトリ名は変更不可！
$ cd obj
$ ruby ../configure.rb -T mbed_m4f_gcc -w -S "syslog.o banner.o serial.o logtask.o" # コアに合わせてターゲット名は変更する
$ make # libasp.a ができる
$ cd ../../
$ mbed-tools compile -m NUCLEO_F401RE -t GCC_ARM # NUCLEO F401RE 向けにビルドする場合
$ mbed-tools compile -m NUCLEO_F401RE -t GCC_ARM --flash # 書き込み
```

sample1.c は sample1.cpp にファイル形式が変更されており、MbedのAPIを利用してLEDを点灯させています。

現在のところ、以下のターゲットで動作を確認しています。

- LPC1768 (Cortex-M3)
- NUCLEO_F401RE (Cortex-M4F)
- NUCLEO_F429ZI (Cortex-M4F)
- NUCLEO_L552ZE_Q (Cortex-M33, TrustZoneなし)

## 実装について

### デフォルトでの割り込み周りの挙動

デフォルトでは全ての割り込みの優先度が0x80になっており、CPUロック状態ではBASEPRIレジスタに0x40を設定します（ARMv6-MではNVICで割り込みの有効/無効を切り替えることで同等の動作を実現）。

割り込みベクタはmbedが持っているためASP3の割り込み出入口処理（`core_int_entry()`）は使われませんが、ほとんどの場合は問題なく動作します。

（唯一、割り込み内で `loc_cpu()` を呼び出した後に `unl_cpu()` を呼ばずに割り込みからリターンした場合は誤動作します。）

### 割り込み優先度を設定したい場合

割り込み優先度を厳密に設定する場合は、以下の手順を踏みます。

1. asp3/arch/arm_m_gcc/mbed/chip_sil.h のマクロ `TBITW_IPRI` をターゲットの割り込み優先度ビット幅に合わせて変更します。
2. asp3/arch/arm_m_gcc/mbed/chip_kernel.h のマクロ `TMIN_INTPRI` をカーネル管理の割り込みがとり得る最高の割り込み優先度に設定します。
3. asp3/arch/arm_m_gcc/mbed/target_kernel_impl.h のマクロ `TMAX_INTNO` をターゲットの割り込み番号の最大値に合わせて変更します。
4. `CFG_INT()` により割り込みの優先度を設定します。

### ASP3の割り込み出入口処理を使いたい場合

ASP3の割り込み出入口処理を使う場合は、以下の手順を踏みます。

1. `CFG_INT()` により割り込みの設定を行います。
2. `DEF_INH()` または `CRE_ISR()` により割り込みハンドラもしくは割り込みサービスルーチンを作成します。

## 元のASP3から変更した箇所

- core_kernel_impl.c
  - `core_initialize()` でVTORレジスタを上書きしないよう変更
  - `core_initialize()` で全ての割り込み優先度を0x80に設定し、CPU例外のハンドラを`VTOR`レジスタが指す割り込みベクタに代入するよう変更
  - `define_inh()`で指定された番号の割り込みに対して、`VTOR`レジスタが指す割り込みベクタに`core_int_entry()`を代入するよう変更
- core_kernel_impl.h
  - `define_inh()`をインライン関数からプロトタイプ宣言に変更
  - `init_tskinictxb` と `term_tskinictxb` を追加
- core_kernel.trb
  - ベクタテーブルのセクション指定を削除（おそらく不要だが一応）
- core_kernel_v6m.trb
  - ベクタテーブルのセクション指定を削除（おそらく不要だが一応）
- startup.c
  - `current_hrtcnt` の初期値を 0 に固定
  - `set_hrt_event()` の呼び出しを削除
- sample/Makefile
  - `APPL_COBJS` から `@(APPLOBJS)` を除外（mbed側でlog_output.c等をビルドさせても良いが、なぜかARMv6-Mではコンパイルエラーが発生した）
  - all のターゲットを lib$(OBJNAME).a に変更
  - lib$(OBJNAME).a のルールを追加
- sample/sample1.c
  - 拡張子をcppに変更
  - `task()`内でLEDを点滅させるよう変更

## 現状の課題

- 割り込みベクタをRAMに持たないターゲットではハングアップすると思われます。
- 割り込み関連APIやARMv6-M (Cortex-M0)はほぼテストできていません。
- MbedのRTOSとの互換性はないので、MbedのRTOSに依存するアプリケーションは動作しません。
  - ASP3の動的生成拡張パッケージを使うか、最初に十分な数のオブジェクトを確保して実行時に割り当てればポーティング可能か。

## 利用条件

本リポジトリの利用条件は[TOPPERSライセンス](https://www.toppers.jp/license.html)に従います。ただし、インポートされるmbedライブラリは別ライセンスになるためご注意ください。

