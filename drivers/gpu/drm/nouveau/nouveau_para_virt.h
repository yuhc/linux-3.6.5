#ifndef __NOUVEAU_PARA_VIRT_H__
#define __NOUVEAU_PARA_VIRT_H__

#define NOUVEAU_PARA_VIRT_REG_BAR 4
#define NOUVEAU_PARA_VIRT_SLOT_SIZE 0x100ULL
#define NOUVEAU_PARA_VIRT_SLOT_NUM 64ULL
#define NOUVEAU_PARA_VIRT_SLOT_TOTAL (NOUVEAU_PARA_VIRT_SLOT_SIZE * NOUVEAU_PARA_VIRT_SLOT_NUM)

int  nouveau_para_virt_init(struct drm_device *);
void nouveau_para_virt_takedown(struct drm_device *);
u8* nouveau_para_virt_alloc_slot(struct drm_device *);
void nouveau_para_virt_free_slot(struct drm_device *, u8 *);
int nouveau_para_virt_call(struct drm_device *, u8 *);

#endif
