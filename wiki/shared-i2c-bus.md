# 共享 I2C 总线上别反复创建销毁设备句柄

## 现象

开机几分钟后偶现，时间不固定：

```
E i2c.master: i2c_master_bus_rm_device(1205): Wrong I2C status, cannot delete device
E i2c_bus: i2c_bus_v2.c:223 (i2c_bus_device_delete):remove device error
```

## 根因

`i2c_master_bus_rm_device()` 在总线上有事务进行时会直接拒绝：

```c
ESP_RETURN_ON_FALSE((((int)atomic_load(&handle->master_bus->status) > (int)I2C_STATUS_START)),
                    ESP_ERR_INVALID_STATE, TAG, "Wrong I2C status, cannot delete device");
```

而 BMI270 的 vendor 封装，**每读写一个寄存器**就创建销毁一次设备句柄：

```c
i2c_bus_device_handle_t i2c_device = i2c_bus_device_create(dev_info->i2c_handle, dev_info->dev_addr, 0);
esp_err_t ret = i2c_bus_read_bytes(i2c_device, reg_addr, len, reg_data);
i2c_bus_device_delete(&i2c_device);
```

这块板子上除了屏幕，PMIC、IO 扩展、触摸、RTC、音频编解码、IMU 全挂在 `I2C_NUM_0` 这一条总线上。IMU 是主循环每 250 ms 轮询一次，触摸是 LVGL 任务每 10 ms 读一次。两者一撞，IMU 那次 delete 正好落在别人的事务中间，就失败了。

被拒绝的 delete 不是没事——设备节点仍然留在总线的设备链表里，**每发生一次泄漏一个**。

## 改法

句柄创建一次，整个传感器生命周期复用。已经打包成 `device/patches/BMI270_BMM150_Sensor.patch`，因为直接改 `device/vendor/BMI270_BMM150_Sensor` 是留不住的，见 [vendor-patching.md](vendor-patching.md)。

## 经验

共享总线上的设备句柄属于初始化期的对象。在热路径上创建或销毁它，就是在和总线上所有其他任务赛跑，而且失败是偶发的、依赖时序的、还会泄漏。vendor 驱动这么干的话，去打补丁，别在调用点绕。

同类形态还可以留意：任何在别的任务可能正在传输时去改**总线级状态**的 API（增删设备、改总线速率）都是一样的坑。M5IOE1 和 M5PM1 两个驱动切 I2C 速率时也是先删后建句柄，形状完全一样——只不过它们发生在开机初始化阶段，所以侥幸没事。
