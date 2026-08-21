# 高斯模糊背景中间的一条横线

## 现象

`fit_blur` 壁纸模式下，模糊背景正中间有一道贯穿整个宽度的水平线，位置固定在屏幕高度的一半。换壁纸位置不变，前景那张清晰的图看着是正常的。

## 根因

`device/components/ui/diagnostics_ui.cpp` 的 `ProductUI::show()` 里：

```cpp
root_ = object(lv_screen_active(), 0, 0, kDisplay, kDisplay, kBase, LV_RADIUS_CIRCLE);
lv_obj_center(root_);
lv_obj_set_style_clip_corner(root_, true, 0);   // <- 就是这一行
```

一个对象只要 `clip_corner` 为真且 radius 非零，LVGL 就会走 `lv_refr.c:167` 的第二个分支：

```c
bool clip_corner = lv_obj_get_style_clip_corner(obj, LV_PART_MAIN);
int32_t radius = 0;
if(clip_corner) {
    radius = lv_obj_get_style_radius(obj, LV_PART_MAIN);
    if(radius == 0) clip_corner = false;
}
if(clip_corner == false) { /* 每个子对象画一次，直接画进父图层 */ }
else                     { /* lv_refr.c:188 */ }
```

那个分支会把**每个子对象画两遍**——一遍进下半图层，一遍进上半图层，都是 `LV_COLOR_FORMAT_ARGB8888`，各 `rout` 行高，各自做圆角遮罩再合成：

```c
int32_t rout = LV_MIN(radius, short_side >> 1);

lv_area_t bottom = obj->coords;
bottom.y1 = bottom.y2 - rout + 1;
layer_children = lv_draw_layer_create(layer, LV_COLOR_FORMAT_ARGB8888, &bottom);
for(i = 0; i < child_cnt; i++) lv_obj_refr(layer_children, obj->spec_attr->children[i]);
lv_draw_mask_rect(layer_children, &mask_draw_dsc);

lv_area_t top = obj->coords;
top.y2 = top.y1 + rout - 1;
/* ...同样再来一遍... */
```

radius 是 `LV_RADIUS_CIRCLE` 时，那个 clamp 会让 `rout` 正好等于对象高度的一半。`root_` 是 466×466 居中在 468×468 的屏上，于是 `rout = 466 >> 1 = 233`，切出 `bottom = 1,234..466,466` 和 `top = 1,1..466,233`。

模糊背景是 `root_` 的子对象，所以它被画了两遍、也被模糊了两遍。而 `lv_draw_sw_blur` 是个 IIR 滤波器，它干的所有事情都是相对于传进来的那个图层：

```c
int32_t layer_x_ofs = t->target_layer->buf_area.x1;
int32_t layer_y_ofs = t->target_layer->buf_area.y1;
lv_area_t clipped_coords;
if(!lv_area_intersect(&clipped_coords, coords, &t->clip_area)) return;
```

滤波器状态是从**这块区域**的头几行开始预热的，上下两半各滤各的，交界处自然出现一个台阶。那就是这条缝。

在 `lv_draw_layer_create` 和 `lv_draw_sw_blur` 里各插一条临时日志，直接拍到了：

```
layer_create: 1,234..466,466 cf=16 parent=0x3fcee8ec
blur: coords -26,-26..493,493 clip 1,234..466,466 ofs 1,234 radius=0(corner=0) ...
layer_create: 1,1..466,233   cf=16 parent=0x3fcee8ec
blur: coords -26,-26..493,493 clip 1,1..466,233   ofs 1,1   radius=0(corner=0) ...
```

`cf=16` 就是 `LV_COLOR_FORMAT_ARGB8888`。下半图层先创建，所以下半屏先被模糊——日志顺序也对得上。

## 改法

把软件圆角裁剪去掉。面板本来就是圆的，466×466 那个方框的四个角早就在可视区外面了，再用软件裁一遍毫无意义。

那条缝只是看得见的症状，真正的代价在后面：**每一帧都要分配两块 466×233 的 ARGB8888 图层（各约 434 kB），并把整棵控件树重绘两遍。**

已在真机验证：线消失了。

## 走过的死路

下面五个假设都跟源码对得上，也全都是错的。每一个都是被实测打掉的，不是被"再读一遍源码"打掉的。

| 假设 | 怎么排除的 |
|---|---|
| `refr_area()` 把帧拆成了 tile | 把算出来的值打出来：`tiles=1 tile_h=468`。`disp->tile_cnt` 只是上限，实际算出来确实是 1。 |
| LVGL 缓冲区不够大，`get_max_row()` 拆了区域 | 缓冲区正好装得下整帧（468×468×2 = 438048 B），flush 日志也显示每帧只有一次整屏刷新。 |
| `LV_LAYER_TYPE_SIMPLE` 分条 | 那条路要求 `opa_layered`、bitmap mask 或 blend mode 三者之一，而背景图用的是普通 `opa`。在那个循环里插了日志，一次都没触发。 |
| 图像缩放是分块做的 | `MAX_BUF_SIZE = 4 * hor_res * px_size = 3744`，`buf_stride = 466 * 3`，所以 `buf_h = 2`。真要出问题会是满屏条纹，不是中间一条线。 |
| M5GFX 推屏时切了 | `Panel_AMOLED_Framebuffer::display()` 是从一块连续内存里逐行 memcpy，`initFramebuffer` 一次性分配、行距也对得上。 |

源图也排除了——在解码出来的缓冲区上跑了同样的探针，它自己的中线位置没有任何异常。

## 经验

圆形面板上不要开软件圆角裁剪。它不光多余，代价还很大——整棵子树的渲染直接翻倍。

LVGL 里凡是**带跨像素状态**的效果（模糊，以及任何基于 IIR 的东西），在图层和 clip 边界上都会断。看到这类效果出现接缝，去找是谁把帧切开了，别在效果本身的参数上调。

顺带一提：`lv_obj_set_style_blur_quality()` 对这个问题完全没用。之前试过改它，白改。
