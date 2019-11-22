#include "updater.h"

#define SHORTREPOID			0x05
#define LONGREPOID			0x06
#define SHORTDEVLENGHT		6
#define LONGDEVLENGHT		1040


#define STATUSCMD				  0xA1


#define CHECKCHECKSUM             0xF0
#define ENTERBOOTLOADER           0xF1
#define ICERASE                   0xF2
#define PROGRAM                   0xF3
#define VERIFY1KDATA              0xF4
#define VERIFY_CHECKSUM           0xF5
#define PROGRAMPASS               0xF6
#define ENDPROGRAM                0xF7
#define I2C_PASS                  0xFA
#define I2C_FAIL                  0xFB
#define I2C_WAIT                  0xFC
#define I2C_TIMEOUT               0xFD

#define CHECKCHECKSUM_PASS              0xE0
#define ENTERBOOTLOADER_PASS            0xE1
#define ICERASE_PASS                    0xE2
#define PROGRAM_PASS                    0xE3
#define VERIFY1KDATA_PASS               0xE4
#define VERIFY_CHECKSUM_PASS            0xE5
#define PROGRAMPASS_PASS                0xE6
#define ENDPROGRAM_PASS                 0xE7

#define CHECKCHECKSUM_FAIL              0xD0
#define ENTERBOOTLOADER_FAIL            0xD1
#define ICERASE_FAIL                    0xD2
#define PROGRAM_FAIL                    0xD3
#define VERIFY1KDATA_FAIL               0xD4
#define VERIFY_CHECKSUM_FAIL            0xD5
#define PROGRAMPASS_FAIL                0xD6
#define ENDPROGRAM_FAIL                 0xD7

#define PROGMODE				  0x0b

static int erase_tp_flash()
{
    unsigned char data[6] = {SHORTREPOID, 0};
    
    printf(">>> Erasing TP flash...: %d\n", data[2]);

    int rc = libusb_control_transfer(devh, 0xa1, 0x01, 0x0305, 1, data, sizeof(data), 2000);
    if (rc < 0) {
        return rc;
    }

    printf(">>> Erased? %02x\n", data[1]);

    if (data[1] != ICERASE_PASS) {
        return -1;
    }
    return 0;
}

int touchpad_verify(int type, int pass)
{
    unsigned char data[6] = {0};
    int rc;

    usleep(50*1000);
    data[0] = SHORTREPOID;
    data[1] = STATUSCMD;
    data[2] = type;
    rc = libusb_control_transfer(devh, 0x21, 0x09, 0x0305, 1, data, sizeof(data), 1000);

    if(rc < 0) {
        return -1;
    }

    usleep(50*1000);

    data[0] = SHORTREPOID;
    rc = libusb_control_transfer(devh, 0xa1, 0x01, 0x0305, 1, data, sizeof(data), 2000);
    if(rc < 0) {
        return -1;
    }

    if (data[1] != pass) {
        return -1;
    }

    return 0;
}

int try_touchpad_verify(int type, int pass)
{
    int try;

    for (try = 0; try < 100; try++) {
        int rc = touchpad_verify(type, pass);
        if (rc == 0) {
            break;
        }
    }

    if (try == 100) {
        printf(">>> Touchpad verify data failed\n");
        return -1;
    }

    return 0;
}

int write_tp_fw(const char *filename)
{
    int len = (24*1024);
    int block_size = 1024;
    FILE *fp = NULL;
    int try;
    int rc;

    printf("[*] Reading %s\n", filename);
    fp = fopen(filename, "rt");
    if (!fp) {
        printf(">>> Failed to read: %s\n", filename);
        goto finish;
    }

    printf("[*] Opening in touchpad mode\n");
    for (try = 0; try < 20; try++) {
        rc = open_touchpad_mode();
        if (rc >= 0) {
            break;
        }
        usleep(50*1000);
    }

    if (try == 100) {
        printf(">>> Failed to open in touchpad mode\n");
        goto finish;
    }

    rc = erase_tp_flash();
    if (rc < 0) {
        printf(">>> Failed to erase flash\n");
        goto finish;
    }

    usleep(10000);

    for(int offset = 0; offset < len; len += block_size)
    {
        unsigned char data[16 + block_size];
        unsigned char *ptr = data;

        *ptr++ = 0x06;
        *ptr++ = 0xD0;
        *ptr++ = 0xCC;
        *ptr++ = 0xCC;
        *ptr++ = offset & 0xFF;
        *ptr++ = (offset >> 8) & 0xFF;
        *ptr++ = 0xCC;
        *ptr++ = 0xCC;

        fread(ptr, 1, block_size, fp);
        ptr += block_size;
        
        *ptr++ = 0xEE;
        *ptr++ = 0xD2;
        *ptr++ = 0xCC;
        *ptr++ = 0xCC;
        *ptr++ = 0xCC;
        *ptr++ = 0xCC;
        *ptr++ = 0xCC;
        *ptr++ = 0xCC;

        printf(">>> Writing offset:%d length:%d...\n", offset, block_size);

        rc = libusb_control_transfer(devh, 0x21, 0x09, 0x0306, 1, data, sizeof(data), 1000);
        if (rc < 0) {
            printf(">>> Write failed\n");
            goto finish;
        }
        
        usleep(150*1000);

        rc = try_touchpad_verify(VERIFY1KDATA, VERIFY1KDATA_PASS);
        if (rc < 0) {
            printf(">>> Touchpad verify data failed\n");
            goto finish;
        }
    }

    usleep(50*1000);

    rc = try_touchpad_verify(ENDPROGRAM, ENDPROGRAM_PASS);
    if (rc < 0) {
        printf(">>> Touchpad end program verify\n");
        goto finish;
    }

    usleep(50*1000);
    
    rc = try_touchpad_verify(VERIFY_CHECKSUM, VERIFY_CHECKSUM_PASS);
    if (rc < 0) {
        printf(">>> Touchpad end program verify\n");
        goto finish;
    }

    usleep(50*1000);
    
    rc = try_touchpad_verify(PROGRAMPASS, 0);
    if (rc < 0) {
        printf(">>> Touchpad end program verify\n");
        goto finish;
    }

finish:
    if(fp) {
        fclose(fp);
    }
    return rc;
}
