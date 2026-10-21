# oWarps Parasites に含まれる第三者コード

oWarps Parasites が配布するものはすべて MIT ライセンスです。GPL / LGPL / AGPL の
コードはコンパイル・リンク・コピーのいずれもしていません。`owarps_parasites.so`
が必要とするのは `libc` と `libm` だけで（そうでなければ `scripts/check_abi.py`
がビルドを失敗させます）、ソースのどこかにコピーレフトの文言が現れれば
`scripts/check_licenses.py` が失敗します。

DSP の出典は Matthias Puech 氏の Parasites リポジトリ（Mutable Instruments の
eurorack リポジトリのフォーク）と、それが submodule として固定している stmlib
だけです。第三者による Schwung / VCV Rack 等の Warps 移植は一切参照していません。

## Mutable Instruments Warps（Parasites ファームウェア）と stmlib — MIT

| | |
|---|---|
| 取得元 | https://github.com/mqtthiqs/parasites（`warps/`）コミット `32fa66f5acce1bff2f0a7bdd041e29bad3222557` |
| | https://github.com/pichenettes/stmlib コミット `8ab2aaee77cbacb47b646d46d22ee5d358effe2d`（Parasites が submodule として固定しているもの） |
| 著作権 | Warps: Copyright 2014–2015 Emilie Gillet（各ファイルのヘッダ）。Parasites の Warps 改変（Doppler / Delay / 各機能モード）は Matthias Puech 氏によるもので、同じファイル・同じ MIT 表記の下にあります。stmlib: Copyright 2012–2015 Olivier Gillet（Emilie Gillet の旧名。各ファイルのヘッダ） |
| ライセンス | MIT。Parasites の README: 「Code (STM32F projects): MIT license」。Warps は STM32F4 のプロジェクトです。取り込んだ全ファイルが下記の MIT 表記を改変せずに保持しています。 |
| 場所 | `src/mi/` |

### 取り込んだファイル（`src/mi/`）

Warps DSP（`warps/`）:

- `dsp/modulator.{h,cc}` — 9 モードすべて（Parasites による Doppler / Delay / Bit Mangler / Comparator / Chebyschev などを含む）
- `dsp/oscillator.{h,cc}`, `dsp/quadrature_oscillator.h` — 内蔵オシレーター
- `dsp/vocoder.{h,cc}`, `dsp/filter_bank.{h,cc}`, `dsp/limiter.h` — ボコーダー
- `dsp/quadrature_transform.h` — 周波数シフター用のヒルベルト変換（全域通過フィルター対）
- `dsp/sample_rate_converter.h`, `dsp/sample_rate_conversion_filters.h` — 内部オーバーサンプリング
- `dsp/parameters.h`
- `resources.{h,cc}` — ルックアップテーブル（正弦、クロスフェード、フォールド、ポットカーブ、全域通過係数、波形テーブル）

stmlib（`stmlib/`）:

- `stmlib.h`, `dsp/{dsp,filter,parameter_interpolator,units,cosine_oscillator}.h`, `dsp/units.cc`
- `utils/random.{h,cc}`
- `LICENSE`（stmlib のライセンスファイル）

それ以外は何も取り込んでいません（ドライバ、UI、CV スケーラ、設定、キャリブレーション、
ブートローダ、ハードウェア設計ファイルは含みません）。`modulator.cc` が include
しているだけで使っていない `warps/drivers/debug_pin.h` は、中身が空の自作ヘッダ
`src/shim/warps/drivers/debug_pin.h` で置き換えています。

ファームウェアの CV/ポット読み取り（`warps/cv_scaler.cc` の `Read()`）は
`src/wrapper/cv_reader.h` に書き写しています（ポットの平滑化係数、ポットカーブ、
LEVEL 入力の正規化の扱いが同じになるよう）。オリジナルの `cv_scaler.cc` は配布物には
含まれず、テスト（`tests/firmware_compare.cpp`）でだけ、取得元からそのまま
コンパイルして書き写しと比較しています。

### 変更点

取り込んだファイルは `patches/` の 3 つのパッチ以外は無改変です。変更箇所には
ソース中にも `oWarps Parasites (Schwung port)` のコメントを付けています。

- `0001-stmlib-interpolate-endpoint-and-arm-float-conversion.patch`
  - `stmlib::Interpolate()` が、小数部 0 のときはテーブルの値そのものを返す（テーブル末尾の 1 つ先を重み 0 で読むのをやめる。値は同じ）。
  - `ArmFloatToInt32()` を追加：float→int32 の変換を、モジュールの Cortex-M4 と Move の AArch64 が行うとおり（飽和、NaN→0）に明示。ARM ではただのキャストです。
- `0002-modulator-per-instance-state-shared-storage-arm-semantics.patch`
  - Delay / Doppler / Chebyschev の関数内 static 変数をメンバーに（複数インスタンスで状態を共有しないため。初期値は同じ）。
  - `delay_buffer_` の末尾を越えて後続メンバーまで遅延線として使う設計を、同じ並びの共用体として明示。
  - Bit Mangler の負のシフト量を Cortex-M4 の結果で明示（AArch64 では結果が異なるため）。
  - 出力変換を `ArmFloatToInt32()` に。
  - Delay の読み出し位置がテープ末尾を越えたときにリングを折り返す（オリジナルはバッファ外の RAM を読む）。
  - 周波数シフターに入るとき、Delay / Doppler と共有している領域にある帰還サンプルを 0 から始める（オリジナルでは NaN を拾って無音のまま固まることがある）。
  - `PrimeDelay()` を追加：インスタンス生成・プリセット読込時に、テープ速度とヘッド間距離の平滑化を「電源投入直後（停止状態から立ち上がる）」ではなく現在のノブ位置から始める。
- `0003-oscillator-arm-float-conversion.patch` — 位相変調入力の float→int32 変換を `ArmFloatToInt32()` に。

`scripts/fetch_warps.sh --verify` が固定コミットを取得し直し、`patches/` を当て、
取り込んだツリーがそれと完全に一致することを確認します。

### MIT License（Warps / Parasites のファイルヘッダの表記）

```
Copyright 2014 Emilie Gillet.

Author: Emilie Gillet (emilie.o.gillet@gmail.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

See http://creativecommons.org/licenses/MIT/ for more information.
```

（各ファイルの年は 2014 または 2015。stmlib のファイルは「Copyright 2012–2015
Olivier Gillet.」で、本文は同一です。stmlib の `LICENSE` は `src/mi/stmlib/LICENSE`
にそのまま入っています。そこに記載されている GPL の `programming/serial` と
STMicroelectronics の `third_party/STM` は取り込んでいません。）

## Schwung プラグイン API ヘッダ — MIT

| | |
|---|---|
| 取得元 | https://github.com/charlesvestal/schwung（`src/host/audio_fx_api_v2.h`, `src/host/plugin_api_v1.h`） |
| 著作権 | Copyright (c) 2025-2026 Charles Vestal |
| 場所 | `third_party/schwung/`（ライセンス全文は `third_party/schwung/LICENSE`） |

ビルド時にだけ使うヘッダで、無改変です。

## このモジュール自身のコード — MIT

`src/plugin.cpp`, `src/wrapper/`, `scripts/`, `tests/` などは
Copyright (c) 2026 Hiro Ikezawa、MIT ライセンス（`LICENSE`）です。
