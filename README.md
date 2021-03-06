# CPU使用率調査コマンド
[![license](https://img.shields.io/github/license/ohmae/CpuUsage.svg)](./LICENSE)

車輪の再発明を通じて勉強しようというコンセプトで
「top」のようにCPU使用率を調査できるコマンド。
「top」ほど高機能ではなく、
/proc/statの情報を元にCPU使用率を調査する非常にシンプルなコマンドです。

主な目的はLinuxにてCPU時間やプロセス・スレッド情報がどのように管理されているかを体験する学習素材として作成しています。

そのため、あまり実用性はないですが、
組み込み用途などでは「top」は高機能すぎて動かすための負荷自体が無視できない場合もあり、
そういった場合に代わりに使うぐらいはできるかもしれません。

なお、複数のバイナリが作成できるようになっており、それぞれで共通となる処理も多いのですが、
シンプルな処理から順に機能を拡張していった体で、順に解説していく素材としても使用するため、
あえて、各バイナリは一つの.cファイルからのみ作成できる構成としています。
モジュールの共通化や、コマンドラインオプションによる動作の切り替えなどは考慮していません。

## Install
インストール手段は用意していません。

```
$ git clone git@github.com:ohmae/CpuUsage.git
$ cd CpuUsage
$ make
```
を実行することで、実行バイナリが作成される。
いずれも引数はなく、実行すると5秒おきに計測結果を表示する。
終了する場合手段も用意していないため `Ctrl-C` で強制終了を行ってください。

## Usage
### cpus
CPU全体の使用率を表示する。
```
$ ./cpus
  0.1% (T:3998 I:3996 IO:   0 S:   1 U:   1 IRQ:   0 G:   0)
  0.0% (T:3996 I:3995 IO:  67 S:   1 U:   0 IRQ:   0 G:   0)
  0.1% (T:3997 I:3994 IO:   0 S:   1 U:   2 IRQ:   0 G:   0)
  0.0% (T:3997 I:3996 IO:   0 S:   1 U:   0 IRQ:   0 G:   0)
...
```

### cpu
CPU全体に加え、マルチCPUの場合、各コアごとの使用率も表示する。

```
$ ./cpu
  load ( total   idle  iowait system   user      irq  guest)  cpu0  cpu1  cpu2  cpu3  cpu4  cpu5  cpu6  cpu7
  4.1% (T:3979 I:3817 IO: 318 S:  36 U: 125 IRQ:   1 G:   0) 10.2%  5.8% 12.9%  2.6%  0.0%  0.8%  0.0%  0.2%
 11.3% (T:3978 I:3530 IO: 285 S:  43 U: 401 IRQ:   4 G:   0) 41.1% 16.0% 15.6%  9.6%  1.6%  1.6%  1.4%  4.0%
  4.6% (T:3965 I:3784 IO: 327 S:  41 U: 137 IRQ:   3 G:   0) 14.5%  6.8%  3.0%  3.0%  2.0%  2.2%  3.0%  1.8%
  1.6% (T:3989 I:3926 IO:  30 S:   9 U:  53 IRQ:   1 G:   0)  3.2%  1.6%  4.6%  1.0%  0.8%  0.6%  0.8%  0.4%
```

### cpup
CPU全体、コアごとの使用率に加え、
プロセス情報を表示する。

```
$ ./cpup
  0.1% (T:4000 I:3998 IO:   2 S:   1 U:   1 IRQ:   0 G:   0)  0.0%  0.4%  0.0%  0.0%  0.2%  0.0%  0.0%  0.0%
237 processes
  PID  PR  NI S    CPU  CNT COMMAND
   69  39  19 S   0.0%    1 khugepaged
   77  20   0 S   0.0%    1 kworker/0:1
  466  20   0 S   0.0%    1 jbd2/sdb1-8
 1091  20   0 S   0.0%    1 Xorg
 1460  20   0 S   0.0%    1 ibus-daemon
 1973  20   0 S   0.0%    1 firefox
 2101  20   0 R   0.0%    1 cpup
    1  20   0 S   0.0%    0 systemd
    2  20   0 S   0.0%    0 kthreadd
    3  20   0 S   0.0%    0 ksoftirqd/0
```

### cput
CPU全体、コアごとの使用率に加え、
スレッド情報を表示する。

```
$ ./cput
  2.4% (T:4022 I:3927 IO:  15 S:  19 U:  75 IRQ:   1 G:   0)  5.8%  6.2%  0.8%  2.6%  0.0%  1.6%  0.8%  1.0%
478 threads
  PID   TID  PR  NI S    CPU  CNT NAME             (COMMAND)
 1091  1091  20   0 S   0.9%   37 Xorg             (Xorg)
 1973  1973  20   0 S   0.7%   30 firefox          (firefox)
 1433  1433  20   0 S   0.1%    5 gnome-shell      (gnome-shell)
 1973  1984  20   0 S   0.1%    4 Socket Thread    (firefox)
 1973  2013  20   0 S   0.1%    3 Compositor       (firefox)
 1973  2023  20   0 S   0.0%    2 SoftwareVsyncTh  (firefox)
   60    60  20   0 S   0.0%    1 rcuos/7          (rcuos/7)
   77    77  20   0 S   0.0%    1 kworker/0:1      (kworker/0:1)
  730   730  20   0 S   0.0%    1 irqbalance       (irqbalance)
 1098  1098  20   0 S   0.0%    1 acpid            (acpid)
```

## Author
大前 良介 (OHMAE Ryosuke)
http://www.mm2d.net/

## License
[MIT License](https://github.com/ohmae/CpuUsage/blob/master/LICENSE.txt)
