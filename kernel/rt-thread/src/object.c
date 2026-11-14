/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2006-03-14     Bernard      the first version
 * 2006-04-21     Bernard      change the scheduler lock to interrupt lock
 * 2006-05-18     Bernard      fix the object init bug
 * 2006-08-03     Bernard      add hook support
 * 2007-01-28     Bernard      rename RT_OBJECT_Class_Static to RT_Object_Class_Static
 * 2010-10-26     yi.qiu       add module support in rt_object_allocate and rt_object_free
 * 2017-12-10     Bernard      Add object_info enum.
 * 2018-01-25     Bernard      Fix the object find issue when enable MODULE.
 * 2022-01-07     Gabriel      Moving __on_rt_xxxxx_hook to object.c
 */

#include <rtthread.h>
#include <rthw.h>

#ifdef RT_USING_MODULE
#include <dlmodule.h>
#endif /* RT_USING_MODULE */

/*
 * define object_info for the number of _object_container items.
 */
enum rt_object_info_type
{
    RT_Object_Info_Thread = 0,                         /**< The object is a thread. */
#ifdef RT_USING_SEMAPHORE
    RT_Object_Info_Semaphore,                          /**< The object is a semaphore. */
#endif
#ifdef RT_USING_MUTEX
    RT_Object_Info_Mutex,                              /**< The object is a mutex. */
#endif
#ifdef RT_USING_EVENT
    RT_Object_Info_Event,                              /**< The object is a event. */
#endif
#ifdef RT_USING_MAILBOX
    RT_Object_Info_MailBox,                            /**< The object is a mail box. */
#endif
#ifdef RT_USING_MESSAGEQUEUE
    RT_Object_Info_MessageQueue,                       /**< The object is a message queue. */
#endif
#ifdef RT_USING_MEMHEAP
    RT_Object_Info_MemHeap,                            /**< The object is a memory heap */
#endif
#ifdef RT_USING_MEMPOOL
    RT_Object_Info_MemPool,                            /**< The object is a memory pool. */
#endif
#ifdef RT_USING_DEVICE
    RT_Object_Info_Device,                             /**< The object is a device */
#endif
    RT_Object_Info_Timer,                              /**< The object is a timer. */
#ifdef RT_USING_MODULE
    RT_Object_Info_Module,                             /**< The object is a module. */
#endif
#ifdef RT_USING_HEAP
    RT_Object_Info_Memory,                            /**< The object is a memory. */
#endif
    RT_Object_Info_Unknown,                            /**< The object is unknown. */
};

#define _OBJ_CONTAINER_LIST_INIT(c)     \
    {&(_object_container[c].object_list), &(_object_container[c].object_list)}

static struct rt_object_information _object_container[RT_Object_Info_Unknown] =
{
    /* initialize object container - thread */
    {RT_Object_Class_Thread, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_Thread), sizeof(struct rt_thread)},
#ifdef RT_USING_SEMAPHORE
    /* initialize object container - semaphore */
    {RT_Object_Class_Semaphore, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_Semaphore), sizeof(struct rt_semaphore)},
#endif
#ifdef RT_USING_MUTEX
    /* initialize object container - mutex */
    {RT_Object_Class_Mutex, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_Mutex), sizeof(struct rt_mutex)},
#endif
#ifdef RT_USING_EVENT
    /* initialize object container - event */
    {RT_Object_Class_Event, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_Event), sizeof(struct rt_event)},
#endif
#ifdef RT_USING_MAILBOX
    /* initialize object container - mailbox */
    {RT_Object_Class_MailBox, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_MailBox), sizeof(struct rt_mailbox)},
#endif
#ifdef RT_USING_MESSAGEQUEUE
    /* initialize object container - message queue */
    {RT_Object_Class_MessageQueue, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_MessageQueue), sizeof(struct rt_messagequeue)},
#endif
#ifdef RT_USING_MEMHEAP
    /* initialize object container - memory heap */
    {RT_Object_Class_MemHeap, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_MemHeap), sizeof(struct rt_memheap)},
#endif
#ifdef RT_USING_MEMPOOL
    /* initialize object container - memory pool */
    {RT_Object_Class_MemPool, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_MemPool), sizeof(struct rt_mempool)},
#endif
#ifdef RT_USING_DEVICE
    /* initialize object container - device */
    {RT_Object_Class_Device, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_Device), sizeof(struct rt_device)},
#endif
    /* initialize object container - timer */
    {RT_Object_Class_Timer, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_Timer), sizeof(struct rt_timer)},
#ifdef RT_USING_MODULE
    /* initialize object container - module */
    {RT_Object_Class_Module, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_Module), sizeof(struct rt_dlmodule)},
#endif
#ifdef RT_USING_HEAP
    /* initialize object container - small memory */
    {RT_Object_Class_Memory, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_Memory), sizeof(struct rt_memory)},
#endif
};

#ifndef __on_rt_object_attach_hook
    #define __on_rt_object_attach_hook(obj)         __ON_HOOK_ARGS(rt_object_attach_hook, (obj))
#endif
#ifndef __on_rt_object_detach_hook
    #define __on_rt_object_detach_hook(obj)         __ON_HOOK_ARGS(rt_object_detach_hook, (obj))
#endif
#ifndef __on_rt_object_trytake_hook
    #define __on_rt_object_trytake_hook(parent)     __ON_HOOK_ARGS(rt_object_trytake_hook, (parent))
#endif
#ifndef __on_rt_object_take_hook
    #define __on_rt_object_take_hook(parent)        __ON_HOOK_ARGS(rt_object_take_hook, (parent))
#endif
#ifndef __on_rt_object_put_hook
    #define __on_rt_object_put_hook(parent)         __ON_HOOK_ARGS(rt_object_put_hook, (parent))
#endif

#if defined(RT_USING_HOOK) && defined(RT_HOOK_USING_FUNC_PTR)
static void (*rt_object_attach_hook)(struct rt_object *object);
static void (*rt_object_detach_hook)(struct rt_object *object);
void (*rt_object_trytake_hook)(struct rt_object *object);
void (*rt_object_take_hook)(struct rt_object *object);
void (*rt_object_put_hook)(struct rt_object *object);

/**
 * @addtogroup Hook
 */

/**@{*/

/**
 * @brief This function will set a hook function, which will be invoked when object
 *        attaches to kernel object system.
 *
 * @param hook is the hook function.
 */
void rt_object_attach_sethook(void (*hook)(struct rt_object *object))
{
    rt_object_attach_hook = hook;
}

/**
 * @brief This function will set a hook function, which will be invoked when object
 *        detaches from kernel object system.
 *
 * @param hook is the hook function
 */
void rt_object_detach_sethook(void (*hook)(struct rt_object *object))
{
    rt_object_detach_hook = hook;
}

/**
 * @brief This function will set a hook function, which will be invoked when object
 *        is taken from kernel object system.
 *
 *        The object is taken means:
 *            semaphore - semaphore is taken by thread
 *            mutex - mutex is taken by thread
 *            event - event is received by thread
 *            mailbox - mail is received by thread
 *            message queue - message is received by thread
 *
 * @param hook is the hook function.
 */
void rt_object_trytake_sethook(void (*hook)(struct rt_object *object))
{
    rt_object_trytake_hook = hook;
}

/**
 * @brief This function will set a hook function, which will be invoked when object
 *        have been taken from kernel object system.
 *
 *        The object have been taken means:
 *            semaphore - semaphore have been taken by thread
 *            mutex - mutex have been taken by thread
 *            event - event have been received by thread
 *            mailbox - mail have been received by thread
 *            message queue - message have been received by thread
 *            timer - timer is started
 *
 * @param hook the hook function.
 */
void rt_object_take_sethook(void (*hook)(struct rt_object *object))
{
    rt_object_take_hook = hook;
}

/**
 * @brief This function will set a hook function, which will be invoked when object
 *        is put to kernel object system.
 *
 * @param hook is the hook function
 */
void rt_object_put_sethook(void (*hook)(struct rt_object *object))
{
    rt_object_put_hook = hook;
}

/**@}*/
#endif /* RT_USING_HOOK */

/**
 * @addtogroup KernelObject
 */

/**@{*/

/**
 * @brief This function will return the specified type of object information.
 *
 * @param type is the type of object, which can be
 *             RT_Object_Class_Thread/Semaphore/Mutex... etc
 *
 * @return the object type information or RT_NULL
 */
struct rt_object_information *
rt_object_get_information(enum rt_object_class_type type)
{
    int index;

    for (index = 0; index < RT_Object_Info_Unknown; index ++)
        if (_object_container[index].type == type) return &_object_container[index];

    return RT_NULL;
}
RTM_EXPORT(rt_object_get_information);

/**
 * @brief This function will return the length of object list in object container.
 *
 * @param type is the type of object, which can be
 *             RT_Object_Class_Thread/Semaphore/Mutex... etc
 *
 * @return the length of object list
 */
int rt_object_get_length(enum rt_object_class_type type)
{
    int count = 0;
    rt_base_t level;
    struct rt_list_node *node = RT_NULL;
    struct rt_object_information *information = RT_NULL;

    information = rt_object_get_information((enum rt_object_class_type)type);
    if (information == RT_NULL) return 0;

    level = rt_hw_interrupt_disable();
    /* get the count of objects */
    rt_list_for_each(node, &(information->object_list))
    {
        count ++;
    }
    rt_hw_interrupt_enable(level);

    return count;
}
RTM_EXPORT(rt_object_get_length);

/**
 * @brief This function will copy the object pointer of the specified type,
 *        with the maximum size specified by maxlen.
 *
 * @param type is the type of object, which can be
 *             RT_Object_Class_Thread/Semaphore/Mutex... etc
 *
 * @param pointers is the pointer will be saved to.
 *
 * @param maxlen is the maximum number of pointers can be saved.
 *
 * @return the copied number of object pointers.
 */
int rt_object_get_pointers(enum rt_object_class_type type, rt_object_t *pointers, int maxlen)
{
    int index = 0;
    rt_base_t level;

    struct rt_object *object;
    struct rt_list_node *node = RT_NULL;
    struct rt_object_information *information = RT_NULL;

    if (maxlen <= 0) return 0;

    information = rt_object_get_information((enum rt_object_class_type)type);
    if (information == RT_NULL) return 0;

    level = rt_hw_interrupt_disable();
    /* retrieve pointer of object */
    rt_list_for_each(node, &(information->object_list))
    {
        object = rt_list_entry(node, struct rt_object, list);

        pointers[index] = object;
        index ++;

        if (index >= maxlen) break;
    }
    rt_hw_interrupt_enable(level);

    return index;
}
RTM_EXPORT(rt_object_get_pointers);

/*
 * Object list integrity guard and lightweight operation trace.
 *
 * A timer object that is freed while it is still linked in the kernel object
 * list leaves a dangling list node behind.  When the heap block is reused as
 * a thread stack, rt_hw_stack_init() fills the top of the stack with
 * 0xdeadbeef, so the linked node's "next" pointer becomes 0xdeadbeef.  The
 * next rt_object_init() then walks that list and takes a load access fault
 * inside the duplicate-check loop.  RT_ASSERT() is compiled out in release
 * builds, so there is no earlier warning.
 *
 * The D21 CarPlay bring-up hit exactly this after the soft-AP had started
 * (the periodic STA auto-connect work timer).  Keep a short ring of object
 * lifetime events and use it to report the broken node instead of faulting,
 * then reset the (already unusable) list head so the system keeps running.
 */
#define RT_OBJECT_TRACE_N       128

struct rt_object_trace
{
    rt_tick_t             tick;
    struct rt_object     *object;
    struct rt_list_node  *next;
    struct rt_list_node  *prev;
    const char           *op;
    char                  name[RT_NAME_MAX];
};

static struct rt_object_trace _object_trace[RT_OBJECT_TRACE_N];
static rt_uint16_t            _object_trace_pos;

static void _object_trace_name(char *dst, const char *src)
{
    int i;

    for (i = 0; i < RT_NAME_MAX - 1; i++)
    {
        char c = src[i];

        if (c == '\0')
            break;
        dst[i] = (c >= 0x20 && c < 0x7f) ? c : '.';
    }
    dst[i] = '\0';
}

static void _object_trace_add(struct rt_object *object, const char *op)
{
    struct rt_object_trace *e;
    rt_base_t level;

    if (object == RT_NULL)
        return;

    level = rt_hw_interrupt_disable();
    e = &_object_trace[_object_trace_pos];
    e->tick   = rt_tick_get();
    e->object = object;
    e->next   = object->list.next;
    e->prev   = object->list.prev;
    e->op     = op;
    _object_trace_name(e->name, object->name);
    _object_trace_pos = (_object_trace_pos + 1) % RT_OBJECT_TRACE_N;
    rt_hw_interrupt_enable(level);
}

static void _object_trace_dump(void)
{
    struct rt_object_trace *e;
    unsigned int i;

    rt_kprintf("[OBJ] last %d object operations (oldest first):\n",
               RT_OBJECT_TRACE_N);
    for (i = 0; i < RT_OBJECT_TRACE_N; i++)
    {
        e = &_object_trace[(_object_trace_pos + i) % RT_OBJECT_TRACE_N];
        if (e->object == RT_NULL)
            continue;
        rt_kprintf("[OBJ]  t=%-10u %-6s obj=%p next=%p prev=%p name='%s'\n",
                   e->tick, e->op, e->object, e->next, e->prev, e->name);
    }
}

/*
 * A list link is usable only if it is aligned and not NULL.  This port sets
 * RT_ALIGN_SIZE to 4, so heap-allocated objects (thread TCBs and their
 * embedded thread_timer) are legitimately only 4-byte aligned even on
 * RV64; do not require sizeof(void *) alignment here.  The corruption we
 * are looking for (thread stack fill 0xdeadbeef) is not 4-byte aligned.
 */
static rt_bool_t _object_list_link_ok(struct rt_list_node *link)
{
    return (link != RT_NULL) &&
           ((((rt_ubase_t)link) % RT_ALIGN_SIZE) == 0);
}

/**
 * @brief This function will initialize an object and add it to object system
 *        management.
 *
 * @param object is the specified object to be initialized.
 *
 * @param type is the object type.
 *
 * @param name is the object name. In system, the object's name must be unique.
 */
void rt_object_init(struct rt_object         *object,
                    enum rt_object_class_type type,
                    const char               *name)
{
    rt_base_t level;
    struct rt_list_node *node = RT_NULL;
    struct rt_list_node *bad_node = RT_NULL;
    struct rt_list_node *bad_next = RT_NULL;
    struct rt_object_information *information;
#ifdef RT_USING_MODULE
    struct rt_dlmodule *module = dlmodule_self();
#endif /* RT_USING_MODULE */

    /* get object information */
    information = rt_object_get_information(type);
    RT_ASSERT(information != RT_NULL);

    /* check object type to avoid re-initialization */

    /* enter critical */
    rt_enter_critical();
    /* try to find object */
    for (node  = information->object_list.next;
            node != &(information->object_list);
            node  = node->next)
    {
        struct rt_object *obj;

        /* never dereference a corrupted node: report it instead */
        if (!_object_list_link_ok(node))
        {
            bad_node = node;
            break;
        }

        obj = rt_list_entry(node, struct rt_object, list);

        /* the node itself is readable, check its forward link as well */
        if (!_object_list_link_ok(obj->list.next))
        {
            bad_node = &(obj->list);
            bad_next = obj->list.next;
            break;
        }

        if (obj) /* skip warning when disable debug */
        {
            RT_ASSERT(obj != object);
        }
    }

    if (bad_node != RT_NULL)
    {
        /*
         * Heal the head while still in the critical section: another thread
         * may create a timer at any moment and would otherwise walk the
         * same broken list.  Report it after the list is usable again.
         */
        rt_list_init(&(information->object_list));
    }
    /* leave critical */
    rt_exit_critical();

    if (bad_node != RT_NULL)
    {
        char bad_name[RT_NAME_MAX] = { 0 };

        if (bad_next != RT_NULL)
        {
            struct rt_object *bad_obj =
                    rt_list_entry(bad_node, struct rt_object, list);

            _object_trace_name(bad_name, bad_obj->name);
            rt_kprintf("[OBJ] corrupt %s list: node=%p name='%s' "
                       "next=%p (expected a pointer), prev=%p\n",
                       type == RT_Object_Class_Timer ? "timer" : "object",
                       bad_node, bad_name, bad_next, bad_obj->list.prev);
            _object_trace_dump();
        }
        else
        {
            rt_kprintf("[OBJ] corrupt %s list: misaligned node=%p "
                       "(cannot dereference)\n",
                       type == RT_Object_Class_Timer ? "timer" : "object",
                       bad_node);
            _object_trace_dump();
        }

        rt_kprintf("[OBJ] class %d object list reset to empty; "
                   "rerun the failing operation to collect a new trace\n",
                   (int)information->type);
    }

    /* initialize object's parameters */
    /* set object type to static */
    object->type = type | RT_Object_Class_Static;
    /* copy name */
    rt_strncpy(object->name, name, RT_NAME_MAX);

    RT_OBJECT_HOOK_CALL(rt_object_attach_hook, (object));

    /* lock interrupt */
    level = rt_hw_interrupt_disable();

#ifdef RT_USING_MODULE
    if (module)
    {
        rt_list_insert_after(&(module->object_list), &(object->list));
        object->module_id = (void *)module;
    }
    else
#endif /* RT_USING_MODULE */
    {
        /* insert object into information object list */
        rt_list_insert_after(&(information->object_list), &(object->list));
    }

    /* unlock interrupt */
    rt_hw_interrupt_enable(level);

    _object_trace_add(object, "init");
}

/**
 * @brief This function will detach a static object from object system,
 *        and the memory of static object is not freed.
 *
 * @param object the specified object to be detached.
 */
void rt_object_detach(rt_object_t object)
{
    rt_base_t level;

    /* object check */
    RT_ASSERT(object != RT_NULL);

    RT_OBJECT_HOOK_CALL(rt_object_detach_hook, (object));

    _object_trace_add(object, "detach");

    /* reset object type */
    object->type = 0;

    /* lock interrupt */
    level = rt_hw_interrupt_disable();

    if (!_object_list_link_ok(object->list.next) ||
        !_object_list_link_ok(object->list.prev))
    {
        char name[RT_NAME_MAX] = { 0 };

        _object_trace_name(name, object->name);
        rt_hw_interrupt_enable(level);
        rt_kprintf("[OBJ] detach of '%s' (%p) has broken links "
                   "next=%p prev=%p; list left untouched\n",
                   name, object, object->list.next, object->list.prev);
        return;
    }

    /* remove from old list */
    rt_list_remove(&(object->list));

    /* unlock interrupt */
    rt_hw_interrupt_enable(level);
}

#ifdef RT_USING_HEAP
/**
 * @brief This function will allocate an object from object system.
 *
 * @param type is the type of object.
 *
 * @param name is the object name. In system, the object's name must be unique.
 *
 * @return object
 */
rt_object_t rt_object_allocate(enum rt_object_class_type type, const char *name)
{
    struct rt_object *object;
    rt_base_t level;
    struct rt_object_information *information;
#ifdef RT_USING_MODULE
    struct rt_dlmodule *module = dlmodule_self();
#endif /* RT_USING_MODULE */

    RT_DEBUG_NOT_IN_INTERRUPT;

    /* get object information */
    information = rt_object_get_information(type);
    RT_ASSERT(information != RT_NULL);

    object = (struct rt_object *)RT_KERNEL_MALLOC(information->object_size);
    if (object == RT_NULL)
    {
        /* no memory can be allocated */
        return RT_NULL;
    }

    /* clean memory data of object */
    rt_memset(object, 0x0, information->object_size);

    /* initialize object's parameters */

    /* set object type */
    object->type = type;

    /* set object flag */
    object->flag = 0;

    /* copy name */
    rt_strncpy(object->name, name, RT_NAME_MAX);

    RT_OBJECT_HOOK_CALL(rt_object_attach_hook, (object));

    /* lock interrupt */
    level = rt_hw_interrupt_disable();

#ifdef RT_USING_MODULE
    if (module)
    {
        rt_list_insert_after(&(module->object_list), &(object->list));
        object->module_id = (void *)module;
    }
    else
#endif /* RT_USING_MODULE */
    {
        /* insert object into information object list */
        rt_list_insert_after(&(information->object_list), &(object->list));
    }

    /* unlock interrupt */
    rt_hw_interrupt_enable(level);

    /* return object */
    return object;
}

/**
 * @brief This function will delete an object and release object memory.
 *
 * @param object is the specified object to be deleted.
 */
void rt_object_delete(rt_object_t object)
{
    rt_base_t level;

    /* object check */
    RT_ASSERT(object != RT_NULL);
    RT_ASSERT(!(object->type & RT_Object_Class_Static));

    RT_OBJECT_HOOK_CALL(rt_object_detach_hook, (object));

    _object_trace_add(object, "delete");

    /* reset object type */
    object->type = RT_Object_Class_Null;

    /* lock interrupt */
    level = rt_hw_interrupt_disable();

    if (!_object_list_link_ok(object->list.next) ||
        !_object_list_link_ok(object->list.prev))
    {
        char name[RT_NAME_MAX] = { 0 };

        _object_trace_name(name, object->name);
        rt_hw_interrupt_enable(level);
        rt_kprintf("[OBJ] delete of '%s' (%p) has broken links "
                   "next=%p prev=%p; list left untouched\n",
                   name, object, object->list.next, object->list.prev);
    }
    else
    {
        /* remove from old list */
        rt_list_remove(&(object->list));

        /* unlock interrupt */
        rt_hw_interrupt_enable(level);
    }

    /* free the memory of object */
    RT_KERNEL_FREE(object);
}
#endif /* RT_USING_HEAP */

/**
 * @brief This function will judge the object is system object or not.
 *
 * @note  Normally, the system object is a static object and the type
 *        of object set to RT_Object_Class_Static.
 *
 * @param object is the specified object to be judged.
 *
 * @return RT_TRUE if a system object, RT_FALSE for others.
 */
rt_bool_t rt_object_is_systemobject(rt_object_t object)
{
    /* object check */
    RT_ASSERT(object != RT_NULL);

    if (object->type & RT_Object_Class_Static)
        return RT_TRUE;

    return RT_FALSE;
}

/**
 * @brief This function will return the type of object without
 *        RT_Object_Class_Static flag.
 *
 * @param object is the specified object to be get type.
 *
 * @return the type of object.
 */
rt_uint8_t rt_object_get_type(rt_object_t object)
{
    /* object check */
    RT_ASSERT(object != RT_NULL);

    return object->type & ~RT_Object_Class_Static;
}

/**
 * @brief This function will find specified name object from object
 *        container.
 *
 * @param name is the specified name of object.
 *
 * @param type is the type of object
 *
 * @return the found object or RT_NULL if there is no this object
 * in object container.
 *
 * @note this function shall not be invoked in interrupt status.
 */
rt_object_t rt_object_find(const char *name, rt_uint8_t type)
{
    struct rt_object *object = RT_NULL;
    struct rt_list_node *node = RT_NULL;
    struct rt_object_information *information = RT_NULL;

    information = rt_object_get_information((enum rt_object_class_type)type);

    /* parameter check */
    if ((name == RT_NULL) || (information == RT_NULL)) return RT_NULL;

    /* which is invoke in interrupt status */
    RT_DEBUG_NOT_IN_INTERRUPT;

    /* enter critical */
    rt_enter_critical();

    /* try to find object */
    rt_list_for_each(node, &(information->object_list))
    {
        object = rt_list_entry(node, struct rt_object, list);
        if (rt_strncmp(object->name, name, RT_NAME_MAX) == 0)
        {
            /* leave critical */
            rt_exit_critical();

            return object;
        }
    }

    /* leave critical */
    rt_exit_critical();

    return RT_NULL;
}

/**@}*/
