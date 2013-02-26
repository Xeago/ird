/*
 * ird.c
 * Display events received from the Apple Infrared Remote.
 * Can also control Preview.app.
 *
 * gcc -Wall -o ird ird.c -framework IOKit -framework Carbon
 */

#define PROGNAME "ird"
#define PROGVERS "1.0"

#include <stdio.h>
#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/errno.h>
#include <sysexits.h>
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDUsageTables.h>
#include <CoreFoundation/CoreFoundation.h>

static struct option
long_options[] = {
    { "help",    no_argument, 0, 'h' },
    { "preview", no_argument, 0, 'p' },
    { 0, 0, 0, 0 },
};

static const char *options = "hp";

IOHIDElementCookie buttonNextID = 0;
IOHIDElementCookie buttonPreviousID = 0;

typedef struct cookie_struct
{
    IOHIDElementCookie gButtonCookie_SystemAppMenu;
    IOHIDElementCookie gButtonCookie_SystemMenuSelect;
    IOHIDElementCookie gButtonCookie_SystemMenuRight;
    IOHIDElementCookie gButtonCookie_SystemMenuLeft;
    IOHIDElementCookie gButtonCookie_SystemMenuUp;
    IOHIDElementCookie gButtonCookie_SystemMenuDown;
} *cookie_struct_t;

static int drivePreview = 0;

void            usage(void);
void            PreviewChangeSlide(IOHIDElementCookie direction);
inline          void print_errmsg_if_io_err(int expr, char *msg);
inline          void print_errmsg_if_err(int expr, char *msg);
void            QueueCallbackFunction(void *target, IOReturn result,
                                      void *refcon, void *sender);
bool            addQueueCallbacks(IOHIDQueueInterface **hqi);
void            processQueue(IOHIDDeviceInterface **hidDeviceInterface,
                             cookie_struct_t cookies);
void            doRun(IOHIDDeviceInterface **hidDeviceInterface,
                      cookie_struct_t cookies);
cookie_struct_t getHIDCookies(IOHIDDeviceInterface122 **handle);
void            createHIDDeviceInterface(io_object_t hidDevice,
                                         IOHIDDeviceInterface ***hdi);
void            setupAndRun(void);

void
usage(void)
{
    printf("%s (version %s)\n", PROGNAME, PROGVERS);
    printf("Displays events received from the Apple Infrared Remote.\n");
    printf("Usage: %s [OPTIONS...]\n\nOptions:\n", PROGNAME);
    printf("  -h, --help    print this help message and exit\n");
    printf("  -p, --preview use forward/backward button presses for Preview slide transition\n");
}

void
PreviewChangeSlide(IOHIDElementCookie button)
{
    if (button == buttonNextID)
        system("/usr/bin/osascript -e \'tell application \"Preview\" to activate\' -e \'tell application \"System Events\" to click menu item \"Next Item\" of menu \"Go\" of menu bar item \"Go\" of menu bar 1 of application process \"Preview\"\' > /dev/null");
    else if (button == buttonPreviousID)
        system("/usr/bin/osascript -e \'tell application \"Preview\" to activate\' -e \'tell application \"System Events\" to click menu item \"Previous Item\" of menu \"Go\" of menu bar item \"Go\" of menu bar 1 of application process \"Preview\"\' > /dev/null");      
}

inline void
print_errmsg_if_io_err(int expr, char *msg)
{
    IOReturn err = (expr);

    if (err != kIOReturnSuccess) {
        fprintf(stderr, "*** %s - %s(%x, %d).\n", msg, mach_error_string(err),
                err, err & 0xffffff);
        fflush(stderr);
        exit(EX_OSERR);
    }
}

inline void
print_errmsg_if_err(int expr, char *msg)
{
    if (expr) {
        fprintf(stderr, "*** %s.\n", msg);
        fflush(stderr);
        exit(EX_OSERR);
    }
}

void
QueueCallbackFunction(void *target, IOReturn result, void *refcon, void *sender)
{
    HRESULT               ret = 0;
    AbsoluteTime          zeroTime = {0,0};
    IOHIDQueueInterface **hqi;
    IOHIDEventStruct      event;
    
    while (!ret) {
        hqi = (IOHIDQueueInterface **)sender;
        ret = (*hqi)->getNextEvent(hqi, &event, zeroTime, 0);
        if (!ret) {
            if (drivePreview) {
                if (event.value) PreviewChangeSlide(event.elementCookie);
            }
            else
                printf("%#lx %s\n", (long unsigned int)event.elementCookie,
                    (event.value) ? "pressed" : "depressed");
        }
    }
}

bool
addQueueCallbacks(IOHIDQueueInterface **hqi)
{
    IOReturn               ret;
    CFRunLoopSourceRef     eventSource;
    IOHIDQueueInterface ***privateData;

    privateData = malloc(sizeof(*privateData));
    *privateData = hqi;

    ret = (*hqi)->createAsyncEventSource(hqi, &eventSource);
    if (ret != kIOReturnSuccess)
        return false;

    ret = (*hqi)->setEventCallout(hqi, QueueCallbackFunction,
                                  NULL, &privateData);
    if (ret != kIOReturnSuccess)
        return false;

    CFRunLoopAddSource(CFRunLoopGetCurrent(), eventSource,
                       kCFRunLoopDefaultMode);
    return true;
}

void
processQueue(IOHIDDeviceInterface **hidDeviceInterface, cookie_struct_t cookies)
{
    HRESULT               result;
    IOHIDQueueInterface **queue;

    queue = (*hidDeviceInterface)->allocQueue(hidDeviceInterface);
    if (!queue) {
        fprintf(stderr, "Failed to allocate event queue.\n");
        return;
    }

    (void)(*queue)->create(queue, 0, 8);

    (void)(*queue)->addElement(queue, cookies->gButtonCookie_SystemAppMenu, 0);
    (void)(*queue)->addElement(queue, cookies->gButtonCookie_SystemMenuSelect, 0);
    (void)(*queue)->addElement(queue, cookies->gButtonCookie_SystemMenuRight, 0);
    (void)(*queue)->addElement(queue, cookies->gButtonCookie_SystemMenuLeft, 0);
    (void)(*queue)->addElement(queue, cookies->gButtonCookie_SystemMenuUp, 0);
    (void)(*queue)->addElement(queue, cookies->gButtonCookie_SystemMenuDown, 0);

    addQueueCallbacks(queue);

    result = (*queue)->start(queue);
    
    CFRunLoopRun();

    result = (*queue)->stop(queue);

    result = (*queue)->dispose(queue);

    (*queue)->Release(queue);
}

void
doRun(IOHIDDeviceInterface **hidDeviceInterface, cookie_struct_t cookies)
{
    IOReturn ioReturnValue;

    ioReturnValue = (*hidDeviceInterface)->open(hidDeviceInterface, 0);

    processQueue(hidDeviceInterface, cookies);

    if (ioReturnValue == KERN_SUCCESS)
        ioReturnValue = (*hidDeviceInterface)->close(hidDeviceInterface);
    (*hidDeviceInterface)->Release(hidDeviceInterface);
}

cookie_struct_t
getHIDCookies(IOHIDDeviceInterface122 **handle)
{
    cookie_struct_t    cookies;
    IOHIDElementCookie cookie;
    CFTypeRef          object;
    long               number;
    long               usage;
    long               usagePage;
    CFArrayRef         elements;
    CFDictionaryRef    element;
    IOReturn           result;

    if ((cookies = (cookie_struct_t)malloc(sizeof(*cookies))) == NULL) {
        fprintf(stderr, "Failed to allocate cookie memory.\n");
        exit(1);
    }

    memset(cookies, 0, sizeof(*cookies));

    if (!handle || !(*handle))
        return cookies;

    result = (*handle)->copyMatchingElements(handle, NULL, &elements);

    if (result != kIOReturnSuccess) {
        fprintf(stderr, "Failed to copy cookies.\n");
        exit(1);
    }

    CFIndex i;
    for (i = 0; i < CFArrayGetCount(elements); i++) {
        element = CFArrayGetValueAtIndex(elements, i);
        object = (CFDictionaryGetValue(element, CFSTR(kIOHIDElementCookieKey)));
        if (object == 0 || CFGetTypeID(object) != CFNumberGetTypeID())
            continue;
        if(!CFNumberGetValue((CFNumberRef) object, kCFNumberLongType, &number))
            continue;
        cookie = (IOHIDElementCookie)number;
        object = CFDictionaryGetValue(element, CFSTR(kIOHIDElementUsageKey));
        if (object == 0 || CFGetTypeID(object) != CFNumberGetTypeID())
            continue;
        if (!CFNumberGetValue((CFNumberRef)object, kCFNumberLongType, &number))
            continue;
        usage = number;
        object = CFDictionaryGetValue(element,CFSTR(kIOHIDElementUsagePageKey));
        if (object == 0 || CFGetTypeID(object) != CFNumberGetTypeID())
            continue;
        if (!CFNumberGetValue((CFNumberRef)object, kCFNumberLongType, &number))
            continue;
        usagePage = number;

        if (usagePage == kHIDPage_GenericDesktop) {
            switch (usage) {
            case kHIDUsage_GD_SystemAppMenu:
                cookies->gButtonCookie_SystemAppMenu = cookie;
                break;
            case kHIDUsage_GD_SystemMenu:
                cookies->gButtonCookie_SystemMenuSelect = cookie;
                break;
            case kHIDUsage_GD_SystemMenuRight:
                buttonNextID = cookie;
                cookies->gButtonCookie_SystemMenuRight = cookie;
                break;
            case kHIDUsage_GD_SystemMenuLeft:
                buttonPreviousID = cookie;
                cookies->gButtonCookie_SystemMenuLeft = cookie;
                break;
            case kHIDUsage_GD_SystemMenuUp:
                cookies->gButtonCookie_SystemMenuUp = cookie;
                break;
            case kHIDUsage_GD_SystemMenuDown:
                cookies->gButtonCookie_SystemMenuDown = cookie;
                break;
            }
        }
    }

    return cookies;
}

void
createHIDDeviceInterface(io_object_t hidDevice, IOHIDDeviceInterface ***hdi)
{
    io_name_t             className;
    IOCFPlugInInterface **plugInInterface = NULL;
    HRESULT               plugInResult = S_OK;
    SInt32                score = 0;
    IOReturn              ioReturnValue = kIOReturnSuccess;

    ioReturnValue = IOObjectGetClass(hidDevice, className);

    print_errmsg_if_io_err(ioReturnValue, "Failed to get class name.");

    ioReturnValue = IOCreatePlugInInterfaceForService(
                        hidDevice,
                        kIOHIDDeviceUserClientTypeID,
                        kIOCFPlugInInterfaceID,
                        &plugInInterface,
                        &score);

    if (ioReturnValue != kIOReturnSuccess)
        return;

    plugInResult = (*plugInInterface)->QueryInterface(
                        plugInInterface,
                        CFUUIDGetUUIDBytes(kIOHIDDeviceInterfaceID),
                        (LPVOID)hdi);
    print_errmsg_if_err(plugInResult != S_OK,
                        "Failed to create device interface.\n");

    (*plugInInterface)->Release(plugInInterface);
}

void
setupAndRun(void)
{
    CFMutableDictionaryRef hidMatchDictionary = NULL;
    io_service_t           hidService = (io_service_t)0;
    io_object_t            hidDevice = (io_object_t)0;
    IOHIDDeviceInterface **hidDeviceInterface = NULL;
    IOReturn               ioReturnValue = kIOReturnSuccess;
    cookie_struct_t        cookies;
    
    hidMatchDictionary = IOServiceNameMatching("AppleIRController");
    hidService = IOServiceGetMatchingService(kIOMasterPortDefault,
                                             hidMatchDictionary);

    if (!hidService) {
        fprintf(stderr, "Apple Infrared Remote not found.\n");
        exit(1);
    }

    hidDevice = (io_object_t)hidService;

    createHIDDeviceInterface(hidDevice, &hidDeviceInterface);
    cookies = getHIDCookies((IOHIDDeviceInterface122 **)hidDeviceInterface);
    ioReturnValue = IOObjectRelease(hidDevice);
    print_errmsg_if_io_err(ioReturnValue, "Failed to release HID.");

    if (hidDeviceInterface == NULL) {
        fprintf(stderr, "No HID.\n");
        exit(1);
    }

    ioReturnValue = (*hidDeviceInterface)->open(hidDeviceInterface, 0);

    doRun(hidDeviceInterface, cookies);

    if (ioReturnValue == KERN_SUCCESS)
        ioReturnValue = (*hidDeviceInterface)->close(hidDeviceInterface);

    (*hidDeviceInterface)->Release(hidDeviceInterface);
}

int
main (int argc, char **argv)
{
    int c, option_index = 0;

    while ((c = getopt_long(argc, argv, options, long_options, &option_index))
         != -1) {
        switch (c) {
        case 'h':
            usage();
            exit(0);
            break;
        case 'p':
            drivePreview = 1;
            break;
        default:
            usage();
            exit(1);
            break;
        }
    }

    setupAndRun();

    return 0;
}
