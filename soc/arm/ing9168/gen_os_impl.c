#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/random/random.h>
#include "port_gen_os_driver.h"
#include "platform_api.h"
#include "ingsoc.h"

#if (CONFIG_HEAP_MEM_POOL_SIZE <= 0)
#error "CONFIG_HEAP_MEM_POOL_SIZE must be defined"
#endif

#if (CONFIG_NUM_PREEMPT_PRIORITIES < 6)
#error "CONFIG_NUM_PREEMPT_PRIORITIES is too small"
#endif

#define Z_PRIORITY_LOW      5
#define Z_PRIORITY_HIGH     2

#define USER_ASSERT

#ifdef  USER_ASSERT
#define GEN_OS_ASSERT(expr) do { if (!(expr)) platform_raise_assertion(__FILE__, __LINE__); } while (0)
#else
#define GEN_OS_ASSERT(expr)
#endif

struct timer_user_data
{
    void *user_data;
    void (* timer_cb)(void *);
    uint32_t timeout_in_ms;
};

static void os_timer_cb(struct k_timer *xTimer)
{
    struct timer_user_data *data = (struct timer_user_data *)xTimer->user_data;
    data->timer_cb(data->user_data);
}

k_timeout_t g_time_set;

static gen_handle_t _port_timer_create(
        uint32_t timeout_in_ms,
        void *user_data,
        void (* timer_cb)(void *)
    )
{
    struct k_timer *timer =(struct k_timer *)k_malloc(sizeof(struct k_timer));
    struct timer_user_data *timer_data = (struct timer_user_data *)k_malloc(sizeof(struct timer_user_data));
    GEN_OS_ASSERT(timer && timer_data);
	k_timer_init(timer, os_timer_cb, NULL);
    timer_data->timeout_in_ms = timeout_in_ms;
    timer_data->timer_cb = timer_cb;
    timer_data->user_data = user_data;
    timer->user_data = timer_data;
    return timer;
}

static void _port_timer_start(gen_handle_t timer)
{
    struct timer_user_data* ptr = ((struct k_timer *)timer)->user_data;
    k_timer_start(timer, K_MSEC(ptr->timeout_in_ms), K_NO_WAIT);
}

static void _port_timer_stop(gen_handle_t timer)
{
    k_timer_stop(timer);
}

static void _port_timer_delete(gen_handle_t timer)
{
    k_timer_stop(timer);
    struct timer_user_data* ptr = ((struct k_timer *)timer)->user_data;
    k_free(ptr);
    k_free(timer);
}

static gen_handle_t _port_task_create(
        const char *name,
        void (*entry)(void *),
        void *parameter,
        uint32_t stack_size,                    // stack size in bytes
        enum gen_os_task_priority priority
    )
{
    struct k_thread *thread = k_malloc(sizeof(*thread));
    k_thread_stack_t *stack = k_thread_stack_alloc(stack_size, K_USER);
    GEN_OS_ASSERT(thread && stack);

    k_tid_t tid = k_thread_create(thread,
                        stack, stack_size,
                        entry,
                        parameter, NULL, NULL,
                        priority == GEN_TASK_PRIORITY_LOW ? Z_PRIORITY_LOW : Z_PRIORITY_HIGH,
                        K_USER, K_MSEC(1));
    k_thread_name_set(tid, name);
    return (gen_handle_t)tid;
}

static gen_handle_t _port_queue_create(int len, int item_size)
{
    struct k_msgq *q = k_malloc(sizeof(*q));
    char *buffer = k_malloc(len * item_size);
    GEN_OS_ASSERT(q && buffer);
    k_msgq_init(q, buffer, item_size, len);
    return q;
}

static int _port_queue_send_msg(gen_handle_t queue, void *msg)
{
    return k_msgq_put((struct k_msgq *)queue, msg, K_NO_WAIT) == 0 ? 0 : 1;
}

static int _port_queue_recv_msg(gen_handle_t queue, void *msg)
{
    return k_msgq_get((struct k_msgq *)queue, msg, K_FOREVER)==0 ? 0 : 1;
}

static gen_handle_t _port_event_create()
{
    gen_handle_t ret = NULL;
    struct k_sem *sem = k_malloc(sizeof(*sem));
    GEN_OS_ASSERT(sem);
    k_sem_init(sem, 0, 1);
    return sem;
}

static int _port_event_wait(gen_handle_t event)
{
    int r = k_sem_take(event, K_FOREVER) == 0 ? 0 : 1;
    return r;
}

static void _port_event_set(gen_handle_t event)
{
    k_sem_give(event);
    return;
}

extern void z_arm_exc_exit(void);
extern void sys_clock_isr(void *arg);
extern void z_cstart(void);
extern int sys_clock_driver_init();
extern void z_cstart0();
extern void z_cstart1();
extern void z_arm_pendsv();

void os_open_psp_control(void)
{
    __asm__ volatile (
    "mrs r0, CONTROL\n\t"
    "movs r1, #2\n\t"
    "orrs r0, r1\n\t"
    "msr CONTROL, r0\n\t"
    "isb\n\t"
    );
}

static void _os_start(void)
{
    os_open_psp_control();

    __enable_irq();
    z_cstart1();
}

#define CS_MAX_NEST_DEPTH 20
static uint32_t interrupt_states[CS_MAX_NEST_DEPTH] = {0};
static int nest_level = 0;

static void _port_enter_critical()
{
    if (nest_level >= sizeof(interrupt_states) / sizeof(interrupt_states[0]))
        platform_raise_assertion(__FILE__, __LINE__);
    interrupt_states[nest_level] = irq_lock();
    nest_level++;
}

static void _port_leave_critical()
{
    nest_level--;
    if (nest_level < 0)
        platform_raise_assertion(__FILE__, __LINE__);
    irq_unlock(interrupt_states[nest_level]);
}

static void * _port_malloc(size_t size)
{
    void *ptr  = k_malloc(size);
    return ptr;
}

static void _port_sys_clock_isr(void)
{
    sys_clock_isr(NULL);
}

const gen_os_driver_t gen_os_driver =
{
    .timer_create   = _port_timer_create,
    .timer_start    = _port_timer_start,
    .timer_stop     = _port_timer_stop,
    .timer_delete   = _port_timer_delete,

    .task_create    = _port_task_create,

    .queue_create   = _port_queue_create,
    .queue_send_msg = _port_queue_send_msg,
    .queue_recv_msg = _port_queue_recv_msg,

    .event_create   = _port_event_create,
    .event_set      = _port_event_set,
    .event_wait     = _port_event_wait,

    .malloc         =  _port_malloc,
    .free           = k_free,
    .enter_critical = _port_enter_critical,
    .leave_critical = _port_leave_critical,
    .os_start       = _os_start,
    .tick_isr       = _port_sys_clock_isr,
    .svc_isr        = z_arm_exc_exit,
    .pendsv_isr     = z_arm_pendsv,
};

#ifdef CONFIG_VECTOR_IN_RAM
#define PLATFORM_NVIC_VECT  0x20000000
#else
#define PLATFORM_NVIC_VECT  0x02002000
#endif

static void override_handler(const void *arg)
{
    int irq = (int)arg + 16;
    typedef void (*f_irq_handler)(void);

    f_irq_handler isr = (f_irq_handler)io_read(PLATFORM_NVIC_VECT + irq * 4);

    isr();
}

static void override_isr_handlers(void)
{
    int i;
    for (i = 0; i < CONFIG_NUM_IRQS; i++)
    {
        irq_connect_dynamic(i, 3, override_handler, (void *)i, 0);
    }

    extern char _vector_start[];
    #define VECTOR_ADDRESS ((uintptr_t)_vector_start)

    SCB->VTOR = VECTOR_ADDRESS;
}

const gen_os_driver_t *os_impl_get_driver(void)
{
    override_isr_handlers();

    z_cstart0();
    return &gen_os_driver;
}

void platform_get_heap_status(platform_heap_status_t *status)
{
    // TODO
}