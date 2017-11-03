/* thetahdr
 *
 * Based largely on the ptpcam utility, Copyright (C) 2001-2005 Mariusz Woloszyn <emsi@ipartners.pl>
 *
 */


#include <iostream>
extern "C" {
   #include <libptp2/ptp.h>
}
#include <cstring>
#include <usb.h>
#include <malloc.h>

#define USB_TIMEOUT		5000
#define PTPCAM_USB_URB  2097152

#define CR(o,error){\
            uint16_t result=o;\
            if((result)!=PTP_RC_OK) {\
                ptp_perror(&params,result);\
                fprintf(stderr,"ERROR: %s", error);\
                close_camera(&ptp_usb, &params, dev);\
                return;\
            }\
}

using namespace std;

short verbose=0;
int ptpcam_usb_timeout = USB_TIMEOUT;

typedef struct _PTP_USB PTP_USB;
struct _PTP_USB {
    usb_dev_handle* handle;
    int inep;
    int outep;
    int intep;
};

struct usb_bus*
init_usb()
{
    usb_init();
    usb_find_busses();
    usb_find_devices();
    return (usb_get_busses());
}

void
close_usb(PTP_USB* ptp_usb, struct usb_device* dev)
{
    //clear_stall(ptp_usb);
        usb_release_interface(ptp_usb->handle,
                dev->config->interface->altsetting->bInterfaceNumber);
    usb_reset(ptp_usb->handle);
        usb_close(ptp_usb->handle);
}

struct usb_device*
find_device (int busn, int devn, short force)
{
    struct usb_bus *bus;
    struct usb_device *dev;

    bus=init_usb();
    for (; bus; bus = bus->next)
    for (dev = bus->devices; dev; dev = dev->next)
    if (dev->config)
    if ((dev->config->interface->altsetting->bInterfaceClass==
        USB_CLASS_PTP)||force)
    if (dev->descriptor.bDeviceClass!=USB_CLASS_HUB)
    {
        int curbusn, curdevn;

        curbusn=strtol(bus->dirname,NULL,10);
        curdevn=strtol(dev->filename,NULL,10);

        if (devn==0) {
            if (busn==0) return dev;
            if (curbusn==busn) return dev;
        } else {
            if ((busn==0)&&(curdevn==devn)) return dev;
            if ((curbusn==busn)&&(curdevn==devn)) return dev;
        }
    }
    return NULL;
}

void
find_endpoints(struct usb_device *dev, int* inep, int* outep, int* intep)
{
    int i,n;
    struct usb_endpoint_descriptor *ep;

    ep = dev->config->interface->altsetting->endpoint;
    n=dev->config->interface->altsetting->bNumEndpoints;

    for (i=0;i<n;i++) {
    if (ep[i].bmAttributes==USB_ENDPOINT_TYPE_BULK)	{
        if ((ep[i].bEndpointAddress&USB_ENDPOINT_DIR_MASK)==
            USB_ENDPOINT_DIR_MASK)
        {
            *inep=ep[i].bEndpointAddress;
            if (verbose>1)
                fprintf(stderr, "Found inep: 0x%02x\n",*inep);
        }
        if ((ep[i].bEndpointAddress&USB_ENDPOINT_DIR_MASK)==0)
        {
            *outep=ep[i].bEndpointAddress;
            if (verbose>1)
                fprintf(stderr, "Found outep: 0x%02x\n",*outep);
        }
        } else if ((ep[i].bmAttributes==USB_ENDPOINT_TYPE_INTERRUPT) &&
            ((ep[i].bEndpointAddress&USB_ENDPOINT_DIR_MASK)==
                USB_ENDPOINT_DIR_MASK))
        {
            *intep=ep[i].bEndpointAddress;
            if (verbose>1)
                fprintf(stderr, "Found intep: 0x%02x\n",*intep);
        }
    }
}

static short
ptp_write_func (unsigned char *bytes, unsigned int size, void *data)
{
    int result;
    PTP_USB *ptp_usb=(PTP_USB *)data;

    result=usb_bulk_write(ptp_usb->handle,ptp_usb->outep,(char *)bytes,size,ptpcam_usb_timeout);
    if (result >= 0)
        return (PTP_RC_OK);
    else
    {
        if (verbose) perror("usb_bulk_write");
        return PTP_ERROR_IO;
    }
}

static short
ptp_read_func (unsigned char *bytes, unsigned int size, void *data)
{
    int result=-1;
    PTP_USB *ptp_usb=(PTP_USB *)data;
    int toread=0;
    signed long int rbytes=size;

    do {
        bytes+=toread;
        if (rbytes>PTPCAM_USB_URB)
            toread = PTPCAM_USB_URB;
        else
            toread = rbytes;
        result=usb_bulk_read(ptp_usb->handle, ptp_usb->inep,(char *)bytes, toread,ptpcam_usb_timeout);
        /* sometimes retry might help */
        if (result==0)
            result=usb_bulk_read(ptp_usb->handle, ptp_usb->inep,(char *)bytes, toread,ptpcam_usb_timeout);
        if (result < 0)
            break;
        rbytes-=PTPCAM_USB_URB;
    } while (rbytes>0);

    if (result >= 0) {
        return (PTP_RC_OK);
    }
    else
    {
        if (verbose) perror("usb_bulk_read");
        return PTP_ERROR_IO;
    }
}

static short
ptp_check_int (unsigned char *bytes, unsigned int size, void *data)
{
    int result;
    PTP_USB *ptp_usb=(PTP_USB *)data;

    result=usb_bulk_read(ptp_usb->handle, ptp_usb->intep,(char *)bytes,size,ptpcam_usb_timeout);
    if (result==0)
        result=usb_bulk_read(ptp_usb->handle, ptp_usb->intep,(char *)bytes,size,ptpcam_usb_timeout);
    if (verbose>2) fprintf (stderr, "USB_BULK_READ returned %i, size=%i\n", result, size);

    if (result >= 0) {
        return result;
    } else {
        if (verbose) perror("ptp_check_int");
        return result;
    }
}

void
ptpcam_debug (void *data, const char *format, va_list args)
{
    if (verbose<2) return;
    vfprintf (stderr, format, args);
    fprintf (stderr,"\n");
    fflush(stderr);
}

void
ptpcam_error (void *data, const char *format, va_list args)
{
    vfprintf (stderr, format, args);
    fprintf (stderr,"\n");
    fflush(stderr);
}

void
init_ptp_usb (PTPParams* params, PTP_USB* ptp_usb, struct usb_device* dev)
{
    usb_dev_handle *device_handle;

    params->write_func=ptp_write_func;
    params->read_func=ptp_read_func;
    params->check_int_func=ptp_check_int;
    params->check_int_fast_func=ptp_check_int;
    params->error_func=ptpcam_error;
    params->debug_func=ptpcam_debug;
    params->sendreq_func=ptp_usb_sendreq;
    params->senddata_func=ptp_usb_senddata;
    params->getresp_func=ptp_usb_getresp;
    params->getdata_func=ptp_usb_getdata;
    params->data=ptp_usb;
    params->transaction_id=0;
    params->byteorder = PTP_DL_LE;

    if ((device_handle=usb_open(dev))){
        if (!device_handle) {
            perror("usb_open()");
            exit(0);
        }
        ptp_usb->handle=device_handle;
        usb_set_configuration(device_handle, dev->config->bConfigurationValue);
        usb_claim_interface(device_handle,
            dev->config->interface->altsetting->bInterfaceNumber);
    }

}


int open_camera (int busn, int devn, short force, PTP_USB *ptp_usb, PTPParams *params, struct usb_device **dev)
{
#ifdef DEBUG
    printf("dev %i\tbus %i\n",devn,busn);
#endif

    *dev=find_device(busn,devn,force);
    if (*dev==NULL) {
        fprintf(stderr,"could not find any device matching given "
        "bus/dev numbers\n");
        exit(-1);
    }
    find_endpoints(*dev,&ptp_usb->inep,&ptp_usb->outep,&ptp_usb->intep);

    init_ptp_usb(params, ptp_usb, *dev);
    if (ptp_opensession(params,1)!=PTP_RC_OK) {
        fprintf(stderr,"ERROR: Could not open session!\n");
        close_usb(ptp_usb, *dev);
        return -1;
    }
    if (ptp_getdeviceinfo(params,&params->deviceinfo)!=PTP_RC_OK) {
        fprintf(stderr,"ERROR: Could not get device info!\n");
        close_usb(ptp_usb, *dev);
        return -1;
    }
    return 0;
}

void
close_camera (PTP_USB *ptp_usb, PTPParams *params, struct usb_device *dev)
{
    if (ptp_closesession(params)!=PTP_RC_OK)
        fprintf(stderr,"ERROR: Could not close session!\n");
    close_usb(ptp_usb, dev);
}

void
display_hexdump(char *data, size_t size)
{
    uint i=0;
    char buffer[50];
    char charBuf[17];

    memset((void*)buffer, 0, 49);
    memset((void*)charBuf, 0, 17);
    for (; i < size; ++i)
    {
        snprintf(&(buffer[(i%16) * 3]) , 4, "%02x ", *(data+i)&0xff);
        if ((data[i] >= ' ' && data[i] <= '^') ||
            (data[i] >= 'a' && data[i] <= '~')) // printable characters
            charBuf[i%16] = data[i];
        else
            charBuf[i%16] = '.';
        if ((i % 16) == 15 || i == (size - 1))
        {
            charBuf[(i%16)+1] = '\0';
            buffer[((i%16) * 3) + 2] = '\0';
            printf("%-48s- %-16s\n", buffer, charBuf);
            memset((void*)buffer, 0, 49);
            memset((void*)charBuf, 0, 17);
        }
    }
}

void
send_generic_request(PTPParams params, uint16_t reqCode, uint32_t *reqParams, uint32_t direction, char *data, long fsize)
{

    PTP_USB ptp_usb;
    struct usb_device *dev;
//    char *data = NULL;
//    long fsize = 0;

    display_hexdump(data, fsize);

    if (direction == PTP_DP_SENDDATA) { // read data from file
//        if (strncmp(data_file, "0x", 2) == 0) {
//            uint len = strlen(data_file);
//            char num[3];
//            uint i;
//            data = (char*)calloc(1,len / 2);

//            num[2] = 0;
//            for (i = 2; i < len ; i += 2) {
//                num[1] = data_file[i];
//                if (i < len-1) {
//                    num[0] = data_file[i];
//                    num[1] = data_file[i+1];
//                }
//                else {
//                    num[0] = data_file[i];
//                    num[1] = 0;
//                }
//                data[fsize] = (char)strtol(num, NULL, 16);
//                ++fsize;
//            }
//        }
//        else {
//            FILE *f = fopen(data_file, "r");
//            if (f) {
//                fseek(f, 0, SEEK_END);
//                fsize = ftell(f);
//                fseek(f, 0, SEEK_SET);
//                data = (char*)calloc(1,fsize + 1);
//                if (fread(data, 1, fsize, f) != fsize) {
//                    fprintf(stderr, "PTP: ERROR: can't read data to send from file '%s'\n", data_file);
//                    free (data);
//                    return;
//                }
//                else {
//                    printf("--- data to send ---\n");
//                    display_hexdump(data, fsize);
//                    printf("--------------------\n");
//                }
//            } else { // error no data to send
//                fprintf(stderr, "PTP: ERROR: file not found '%s'\n", data_file);
//                return;
//            }
//        }
    }


    printf("Sending generic request: reqCode=0x%04x, params=[0x%08x,0x%08x,0x%08x,0x%08x,0x%08x]\n",
            (uint)reqCode, reqParams[0], reqParams[1], reqParams[2], reqParams[3], reqParams[4]);
    uint16_t result=ptp_sendgenericrequest (&params, reqCode, reqParams, &data, direction, fsize);
    if((result)!=PTP_RC_OK) {
        ptp_perror(&params,result);
        if (result > 0x2000)
            fprintf(stderr,"PTP: ERROR: response 0x%04x\n", result);
    } else {
        if (data != NULL && direction == PTP_DP_GETDATA) {
            display_hexdump(data, malloc_usable_size ((void*)data));
            //free(data);
        }
        printf("PTP: response OK\n");
    }
    if (data != NULL && direction == PTP_DP_SENDDATA) {
        //free(data);
    }

    return;

}

void
capture_image (int busn, int devn, short force)
{
    PTPParams params;
    PTP_USB ptp_usb;
    PTPContainer event;
    int ExposureTime=0;
    struct usb_device *dev;
    short ret;

    printf("\nInitiating captue...\n");
    if (open_camera(busn, devn, force, &ptp_usb, &params, &dev)<0)
        return;

    if (!ptp_operation_issupported(&params, PTP_OC_InitiateCapture))
    {
        printf ("Your camera does not support InitiateCapture operation!\nSorry, blame the %s!\n", params.deviceinfo.Manufacturer);
        goto out;
    }

    /* obtain exposure time in miliseconds */
    if (ptp_property_issupported(&params, PTP_DPC_ExposureTime))
    {
        PTPDevicePropDesc dpd;
        memset(&dpd,0,sizeof(dpd));
        ret=ptp_getdevicepropdesc(&params,PTP_DPC_ExposureTime,&dpd);
        if (ret==PTP_RC_OK) ExposureTime=(*(int32_t*)(dpd.CurrentValue))/10;
    }

    /* adjust USB timeout */
    if (ExposureTime>USB_TIMEOUT) ptpcam_usb_timeout=ExposureTime;

    CR(ptp_initiatecapture (&params, 0x0, 0), "Could not capture.\n");

    ret=ptp_usb_event_wait(&params,&event);
    if (ret!=PTP_RC_OK) goto err;
    if (verbose) printf ("Event received %08x, ret=%x\n", event.Code, ret);
    if (event.Code==PTP_EC_CaptureComplete) {
        printf ("Camera reported 'capture completed' but the object information is missing.\n");
        goto out;
    }

    while (event.Code==PTP_EC_ObjectAdded) {
        printf ("Object added 0x%08lx\n", (long unsigned) event.Param1);
        if (ptp_usb_event_wait(&params, &event)!=PTP_RC_OK)
            goto err;
        if (verbose) printf ("Event received %08x, ret=%x\n", event.Code, ret);
        if (event.Code==PTP_EC_CaptureComplete) {
            printf ("Capture completed successfully!\n");
            goto out;
        }
    }

    err:
        printf("Events receiving error. Capture status unknown.\n");
    out:

        ptpcam_usb_timeout=USB_TIMEOUT;
        close_camera(&ptp_usb, &params, dev);
}

void intToRevChar4Array(int n, char bytes[4]){
        bytes[3] = (n >> 24) & 0xFF;
        bytes[2] = (n >> 16) & 0xFF;
        bytes[1] = (n >> 8) & 0xFF;
        bytes[0] = n & 0xFF;
}

void setProperty(PTPParams params, int propCode, int value){

    uint32_t reqParams[5] = {propCode,0,0,0,0};

    char data[4];

    intToRevChar4Array(value, data);

    send_generic_request(params, 0x1016, reqParams, PTP_DP_SENDDATA, (char*)data, 4);

}

void setProperty(PTPParams params, int propCode, int value0, int value1){

    uint32_t reqParams[5] = {propCode,0,0,0,0};

    char data[8];

    intToRevChar4Array(value0, data);
    intToRevChar4Array(value1, data+4);

    send_generic_request(params, 0x1016, reqParams, PTP_DP_SENDDATA, (char*)data, 8);
}


int main(int argc, char *argv[])
{

    PTPParams params;
    PTP_USB ptp_usb;
    struct usb_device *dev;
    int busn = 0;
    int devn = 0;
    short force = 0;

    printf("\nCamera information\n");
    printf("==================\n");
    if (open_camera(busn, devn, force, &ptp_usb, &params, &dev)<0)
        return -1;
    printf("Model: %s\n",params.deviceinfo.Model);
    printf("  manufacturer: %s\n",params.deviceinfo.Manufacturer);
    printf("  serial number: '%s'\n",params.deviceinfo.SerialNumber);
    printf("  device version: %s\n",params.deviceinfo.DeviceVersion);
    printf("  extension ID: 0x%08lx\n",(long unsigned)
                    params.deviceinfo.VendorExtensionID);
    printf("  extension description: %s\n",
                    params.deviceinfo.VendorExtensionDesc);
    printf("  extension version: 0x%04x\n",
                params.deviceinfo.VendorExtensionVersion);
    printf("\n");

    // Exposure time
    setProperty(params, 0xd00f, 1, 25000);

    // Audio volume
    setProperty(params, 0x502c, 90);

    close_camera(&ptp_usb, &params, dev);

    return 0;
}
