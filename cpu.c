/**
 * @file cpu.c
 *
 * Copyright(c) 2016 大前良介(OHMAE Ryosuke)
 *
 * This software is released under the MIT License.
 * http://opensource.org/licenses/MIT
 *
 * @brief CPU使用率調査コマンド
 *
 * CPU全体の使用率とコアごとの使用率の表示
 *
 * @author <a href="mailto:ryo@mm2d.net">大前良介(OHMAE Ryosuke)</a>
 * @date 2016/3/21
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include "def.h"

/**
 * ラインバッファのサイズ
 */
#define LINE_BUFFER_SIZE 1024
/**
 * 2つのポインタ値の入れ替え
 */
#define SWAP(a, b) {void *tmp = a; a = b; b = tmp;}

/**
 * statのCPUカウンタ記録用構造体
 */
typedef struct cputime_t {
  uint64_t user;       /**< ユーザーモードで消費した時間 */
  uint64_t nice;       /**< 低い優先度のユーザーモードで消費した時間 */
  uint64_t system;     /**< システムモードで消費した時間 */
  uint64_t idle;       /**< タスク待ちで消費した時間 */
  uint64_t iowait;     /**< I/O の完了待ちの時間 */
  uint64_t irq;        /**< 割り込みの処理に使った時間 */
  uint64_t softirq;    /**< ソフト割り込みの処理に使った時間 */
  uint64_t steal;      /**< 仮想環境下で他のOSに消費された時間 */
  uint64_t guest;      /**< ゲストOSの実行に消費された時間 */
  uint64_t guest_nice; /**< 低い優先度のゲストOSの実行に消費された時間 */
} cputime_t;

/**
 * 全CPU時間格納構造体
 */
typedef struct cpu_t {
  int num;            /**< CPUの個数 */
  cputime_t *times;   /**< CPU時間 */
} cpu_t;

static void *xmalloc(size_t size);
static cpu_t *new_cpu_t(int num);
static void delete_cpu_t(cpu_t *cpu);
static uint64_t get_total(cputime_t *time);
static uint64_t get_load(cputime_t *time);
static uint64_t get_idle(cputime_t *time);
static uint64_t get_iowait(cputime_t *time);
static uint64_t get_system(cputime_t *time);
static uint64_t get_user(cputime_t *time);
static uint64_t get_irq(cputime_t *time);
static uint64_t get_guest(cputime_t *time);
static void get_diff(cputime_t *before, cputime_t *after, cputime_t *diff);
static result_t read_stat(cpu_t *cpu);
static void show_title(int num);
static void show_result(cpu_t *before, cpu_t *after);

/**
 * @brief malloc結果がNULLだった場合にexitする
 *
 * @param[in] size 確保サイズ
 * @return  確保された領域へのポインタ
 */
static void *xmalloc(size_t size) {
  void *p = malloc(size);
  if (p == NULL) {
    ERR("%s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  return p;
}

/**
 * @brief 全CPU時間格納構造体の初期化を行う
 *
 * メモリ確保できない場合はexitする
 *
 * @param[in] num CPUの個数
 * @return 全CPU時間格納構造体
 */
static cpu_t *new_cpu_t(int num) {
  cpu_t *cpu;
  cpu = xmalloc(sizeof(cpu_t));
  cpu->num = num;
  cpu->times = xmalloc((num + 1) * sizeof(cputime_t));
  return cpu;
}

/**
 * @brief 全CPU時間格納構造体の開放を行う
 *
 * @param[in] cpu 開放する構造体
 */
static void delete_cpu_t(cpu_t *cpu) {
  if (cpu == NULL) {
    return;
  }
  free(cpu->times);
  free(cpu);
}

/**
 * @brief カウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return カウンタの合計値
 */
static uint64_t get_total(cputime_t *time) {
  return time->user + time->nice + time->system
      + time->idle + time->iowait + time->irq + time->softirq
      + time->steal + time->guest + time->guest_nice;
}
/**
 * @brief 負荷のカウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return 負荷のカウンタの合計値
 */
static uint64_t get_load(cputime_t *time) {
  return time->user + time->nice + time->system
      + time->irq + time->softirq
      + time->steal + time->guest + time->guest_nice;
}

/**
 * @brief アイドルのカウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return アイドルのカウンタの合計値
 */
static uint64_t get_idle(cputime_t *time) {
  return time->idle + time->iowait;
}

/**
 * @brief I/O待ちのカウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return I/O待ちのカウンタの合計値
 */
static uint64_t get_iowait(cputime_t *time) {
  return time->iowait;
}

/**
 * @brief systemのカウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return systemのカウンタの合計値
 */
static uint64_t get_system(cputime_t *time) {
  return time->system;
}

/**
 * @brief userのカウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return userのカウンタの合計値
 */
static uint64_t get_user(cputime_t *time) {
  return time->user + time->nice;
}

/**
 * @brief 割り込みのカウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return 割り込みのカウンタの合計値
 */
static uint64_t get_irq(cputime_t *time) {
  return time->irq + time->softirq;
}

/**
 * @brief ゲストのカウンタの合計を返す
 *
 * @param[in] time cputime_t構造体
 * @return ゲストのカウンタの合計値
 */
static uint64_t get_guest(cputime_t *time) {
  return time->guest + time->guest_nice;
}

/**
 * @brief cputime_t間の差分を取る
 *
 * @param[in] before 時間的に前の値
 * @param[in] after 時間的に後の値
 * @param[out] diff 差分の格納先
 */
static void get_diff(cputime_t *before, cputime_t *after, cputime_t *diff) {
  uint64_t tmp;
  // 負の値の場合は0とする
#define DIFF(x) {tmp = after->x - before->x; diff->x = tmp < 0 ? 0 : tmp;}
  DIFF(user);
  DIFF(nice);
  DIFF(system);
  DIFF(idle);
  DIFF(iowait);
  DIFF(irq);
  DIFF(softirq);
  DIFF(steal);
  DIFF(guest);
  DIFF(guest_nice);
#undef DIFF
}

/**
 * @brief statの値を読みだす
 *
 * @param[out] cpu 結果の書き込み先
 * @return 成功：SUCCESS / 失敗：FAILURE
 */
static result_t read_stat(cpu_t *cpu) {
  result_t result = FAILURE;
  cputime_t *work;
  FILE *file;
  char line[LINE_BUFFER_SIZE];
  file = fopen("/proc/stat", "rb");
  if (file == NULL) {
    ERR("%s\n", strerror(errno));
    return FAILURE;
  }
  if (fgets(line, sizeof(line), file) == NULL) {
    ERR("%s\n", strerror(errno));
    goto error;
  }
  memset(cpu->times, 0, sizeof(cputime_t) * (cpu->num + 1));
  work = &cpu->times[cpu->num];
  if (sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
             &work->user,
             &work->nice,
             &work->system,
             &work->idle,
             &work->iowait,
             &work->irq,
             &work->softirq,
             &work->steal,
             &work->guest,
             &work->guest_nice) < 4) {
    ERR("%s\n", strerror(errno));
    goto error;
  }
  if (cpu->num > 1) {
    int i;
    for (i = 0; i < cpu->num; i++) {
      if (fgets(line, sizeof(line), file) == NULL) {
        ERR("%s\n", strerror(errno));
        goto error;
      }
      work = &cpu->times[i];
      if (sscanf(line, "cpu%*u %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                 &work->user,
                 &work->nice,
                 &work->system,
                 &work->idle,
                 &work->iowait,
                 &work->irq,
                 &work->softirq,
                 &work->steal,
                 &work->guest,
                 &work->guest_nice) < 4) {
        ERR("%s\n", strerror(errno));
        goto error;
      }
    }
  }
  result = SUCCESS;
  error:
  fclose(file);
  return result;
}

/**
 * @brief 結果表示
 *
 * @param[in] before 時間的に前の値
 * @param[in] after  時間的に後の値
 */
static void show_result(cpu_t *before, cpu_t *after) {
  cputime_t diff;
  int num = before->num;
  get_diff(&before->times[num], &after->times[num], &diff);
  uint64_t total  = get_total(&diff);
  uint64_t load   = get_load(&diff);
  uint64_t idle   = get_idle(&diff);
  uint64_t iowait = get_iowait(&diff);
  uint64_t system = get_system(&diff);
  uint64_t user   = get_user(&diff);
  uint64_t irq    = get_irq(&diff);
  uint64_t guest  = get_guest(&diff);
  if (total == 0) {
    total = 1;
  }
  float usage = (float) load / total * 100;
  printf("%5.1f%% (T:%4lu I:%4lu IO:%4lu S:%4lu U:%4lu IRQ:%4lu G:%4lu)",
         usage, total, idle, iowait, system, user, irq, guest);
  if (num > 1) {
    int i;
    for (i = 0; i < num; i++) {
      get_diff(&before->times[i], &after->times[i], &diff);
      total  = get_total(&diff);
      load   = get_load(&diff);
      if (total == 0) {
        total = 1;
      }
      printf("%5.1f%%", (float) load / total * 100);
    }
  }
  printf("\n");
}

/**
 * @brief 結果のカラムタイトル表示
 *
 * @param[in] num CPUの個数
 */
static void show_title(int num) {
  printf("  load ( total   idle  iowait system   user      irq  guest)" );
  if (num > 1) {
    int i;
    for (i = 0; i < num; i++) {
      printf("  cpu%d", i);
    }
  }
  printf("\n");
}

int main(int argc, char **argv) {
  int result = EXIT_FAILURE;
  cpu_t *after;
  cpu_t *before;
  int num = sysconf(_SC_NPROCESSORS_ONLN);
  after = new_cpu_t(num);
  before = new_cpu_t(num);
  show_title(num);
  if (read_stat(before) != SUCCESS) {
    goto error;
  }
  while (TRUE) {
    sleep(5);
    if (read_stat(after) != SUCCESS) {
      goto error;
    }
    show_result(before, after);
    SWAP(before, after);
  }
  result = EXIT_SUCCESS;
  error:
  delete_cpu_t(before);
  delete_cpu_t(after);
  return result;
}
