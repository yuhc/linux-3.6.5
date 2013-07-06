/*
 * Copyright 2013 Yusuke Suzuki
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/swab.h>
#include <linux/slab.h>
#include "drmP.h"
#include "drm.h"
#include "drm_sarea.h"
#include "drm_crtc_helper.h"
#include <linux/vgaarb.h>
#include <linux/bitops.h>
#include <linux/vga_switcheroo.h>
#include "nouveau_drv.h"
#include "nouveau_para_virt.h"

struct nouveau_para_virt_priv {
	struct drm_device *dev;
	spinlock_t lock;
	spinlock_t slot_lock;
	u8 __iomem *slot;
	u8 __iomem *mmio;
	u64 used_slot;
	struct semaphore sema;
};

static inline u64
nvc0_vm_addr(struct nouveau_vma *vma, u64 phys, u32 memtype, u32 target)
{
	phys >>= 8;

	phys |= 0x00000001; /* present */
	if (vma->access & NV_MEM_ACCESS_SYS)
		phys |= 0x00000002;

	phys |= ((u64)target  << 32);
	phys |= ((u64)memtype << 36);

	return phys;
}

static inline u32 nvpv_rd32(struct nouveau_para_virt_priv *priv, unsigned reg) {
	return ioread32_native(priv->mmio + reg);
}

static inline void nvpv_wr32(struct nouveau_para_virt_priv *priv, unsigned reg, u32 val) {
	iowrite32_native(val, priv->mmio + reg);
}

static inline u32 slot_pos(struct nouveau_para_virt_priv *priv, struct nouveau_para_virt_slot *slot) {
	return (slot->u8 - priv->slot) / NOUVEAU_PV_SLOT_SIZE;
}

struct nouveau_para_virt_slot* nouveau_para_virt_alloc_slot(struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_para_virt_engine *engine = &dev_priv->engine.para_virt;
	struct nouveau_para_virt_priv *priv = engine->priv;

	u32 pos;
	unsigned long flags;
	u8* ret = NULL;

	down(&priv->sema);
	spin_lock_irqsave(&priv->slot_lock, flags);

	pos = fls64(priv->used_slot) - 1;
	priv->used_slot &= ~((0x1ULL) << pos);
	ret = priv->slot + pos * NOUVEAU_PV_SLOT_SIZE;

	spin_unlock_irqrestore(&priv->slot_lock, flags);

	// NV_INFO(dev, "alloc virt space 0x%llx pos %u\n", (u64)(ret), pos);
	return (struct nouveau_para_virt_slot*)ret;
}

void nouveau_para_virt_free_slot(struct drm_device *dev, struct nouveau_para_virt_slot *slot) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_para_virt_engine *engine = &dev_priv->engine.para_virt;
	struct nouveau_para_virt_priv *priv = engine->priv;
	unsigned long flags;
	u32 pos = slot_pos(priv, slot);

	spin_lock_irqsave(&priv->slot_lock, flags);
	priv->used_slot |= ((0x1ULL) << pos);
	spin_unlock_irqrestore(&priv->slot_lock, flags);
	up(&priv->sema);
	// NV_INFO(dev, "free virt space 0x%llx pos %u\n", (u64)(slot), pos);
}

int nouveau_para_virt_call(struct drm_device *dev, struct nouveau_para_virt_slot *slot) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_para_virt_engine *engine = &dev_priv->engine.para_virt;
	struct nouveau_para_virt_priv *priv = engine->priv;
	u32 pos = slot_pos(priv, slot);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	nvpv_wr32(priv, 0xc, pos);  // invoke A3 call
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

int  nouveau_para_virt_init(struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_para_virt_engine *engine = &dev_priv->engine.para_virt;
	struct pci_dev *pdev = dev->pdev;
	struct nouveau_para_virt_priv *priv;
	u64 address;

	NV_INFO(dev, "para virt init start\n");

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		return -ENOMEM;
	}
	engine->priv = priv;

	priv->dev = dev;
	priv->used_slot = ~(0ULL);
	spin_lock_init(&priv->slot_lock);
	spin_lock_init(&priv->lock);
	sema_init(&priv->sema, NOUVEAU_PV_SLOT_NUM);

	if (!(priv->slot = kzalloc(NOUVEAU_PV_SLOT_TOTAL, GFP_KERNEL))) {
		return -ENOMEM;
	}
	((u32*)priv->slot)[0] = 0xdeadbeefUL;

	// map BAR4
	priv->mmio = (u8 __iomem *)ioremap(pci_resource_start(pdev, NOUVEAU_PV_REG_BAR), pci_resource_len(pdev, NOUVEAU_PV_REG_BAR));
	if (!priv->mmio) {
		return -ENODEV;
	}

	// notify this physical address to A3
	address = __pa(priv->slot);  // convert kmalloc-ed virt to phys
	nvpv_wr32(priv, 0x4, lower_32_bits(address));
	nvpv_wr32(priv, 0x8, upper_32_bits(address));
	if (nvpv_rd32(priv, 0x0) != 0x0) {
		return -ENODEV;
	}
	NV_INFO(dev, "para virt slot address 0x%llx\n", address);

	return 0;
}

void nouveau_para_virt_takedown(struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_para_virt_engine *engine = &dev_priv->engine.para_virt;
	struct nouveau_para_virt_priv *priv = engine->priv;

	engine->priv = NULL;
	if (!priv) {
		return;
	}

	if (priv->slot) {
		kfree(priv->slot);
	}

	if (priv->mmio) {
		iounmap(priv->mmio);
	}
	kfree(priv);
}

int nouveau_para_virt_mem_new(struct drm_device *dev, u32 size, struct nouveau_para_virt_mem **ret) {
	struct nouveau_para_virt_mem* obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj) {
		return -ENOMEM;
	}

	obj->dev = dev;
	kref_init(&obj->refcount);
	obj->size = size;

	{
		int ret;
		struct nouveau_para_virt_slot* slot = nouveau_para_virt_alloc_slot(dev);
		slot->u8[0] = NOUVEAU_PV_OP_MEM_ALLOC;
		slot->u32[1] = size;
		nouveau_para_virt_call(dev, slot);

		ret = slot->u32[0];
		obj->id = slot->u32[1];

		nouveau_para_virt_free_slot(dev, slot);

		if (ret) {
			nouveau_para_virt_mem_ref(NULL, &obj);
			return ret;
		}
	}

	*ret = obj;
	return 0;
}

static void nouveau_para_virt_mem_del(struct kref *ref) {
	struct nouveau_para_virt_mem *obj = container_of(ref, struct nouveau_para_virt_mem, refcount);
	struct drm_device* dev = obj->dev;
	{
		struct nouveau_para_virt_slot* slot = nouveau_para_virt_alloc_slot(dev);
		slot->u8[0] = NOUVEAU_PV_OP_MEM_FREE;
		slot->u32[1] = obj->id;
		nouveau_para_virt_call(dev, slot);
		nouveau_para_virt_free_slot(dev, slot);
	}
	kfree(obj);
}

void nouveau_para_virt_mem_ref(struct nouveau_para_virt_mem *ref, struct nouveau_para_virt_mem **ptr) {
	if (ref) {
		kref_get(&ref->refcount);
	}

	if (*ptr) {
		kref_put(&(*ptr)->refcount, nouveau_para_virt_mem_del);
	}

	*ptr = ref;
}

int nouveau_para_virt_set_pgd(struct nouveau_channel* chan, struct nouveau_para_virt_mem* pgd, int id) {
	struct drm_device* dev = chan->dev;
	int ret;
	struct nouveau_para_virt_slot* slot = nouveau_para_virt_alloc_slot(dev);
	slot->u8[0] = NOUVEAU_PV_OP_SET_PGD;
	slot->u32[1] = pgd->id;
	slot->u32[2] = id;
	nouveau_para_virt_call(dev, slot);

	ret = slot->u32[0];

	nouveau_para_virt_free_slot(dev, slot);

	return ret;
}

int nouvaeu_para_virt_map_pgt(struct nouveau_para_virt_mem *pgd, u32 index, struct nouveau_para_virt_mem *pgt[2]) {
	struct drm_device* dev = pgd->dev;
	int ret;
	struct nouveau_para_virt_slot* slot = nouveau_para_virt_alloc_slot(dev);
	slot->u8[0] = NOUVEAU_PV_OP_MAP_PGT;
	slot->u32[1] = pgd->id;
	slot->u32[2] = (pgt[0]) ? pgt[0]->id : 0;
	slot->u32[3] = (pgt[1]) ? pgt[1]->id : 0;
	slot->u32[4] = index;
	nouveau_para_virt_call(dev, slot);

	ret = slot->u32[0];

	nouveau_para_virt_free_slot(dev, slot);

	return ret;
}

int nouveau_para_virt_map(struct nouveau_para_virt_mem *pgt, u32 index, u64 phys) {
	struct drm_device* dev = pgt->dev;
	int ret;
	struct nouveau_para_virt_slot* slot = nouveau_para_virt_alloc_slot(dev);
	slot->u8[0] = NOUVEAU_PV_OP_MAP;
	slot->u32[1] = pgt->id;
	slot->u32[2] = index;
	slot->u64[2] = phys;
	nouveau_para_virt_call(dev, slot);

	ret = slot->u32[0];

	nouveau_para_virt_free_slot(dev, slot);

	return ret;
}

int nouveau_para_virt_map_batch(struct nouveau_para_virt_mem *pgt, u32 index, u64 phys, u32 next, u32 count) {
	struct drm_device* dev = pgt->dev;
	int ret;
	struct nouveau_para_virt_slot* slot = nouveau_para_virt_alloc_slot(dev);
	slot->u8[0] = NOUVEAU_PV_OP_MAP_BATCH;
	slot->u32[1] = pgt->id;
	slot->u32[2] = index;
	slot->u32[3] = next;
	slot->u32[4] = count;
	slot->u64[3] = phys;
	nouveau_para_virt_call(dev, slot);

	ret = slot->u32[0];

	nouveau_para_virt_free_slot(dev, slot);
	return ret;
}

int nouveau_para_virt_map_sg_batch(struct nouveau_para_virt_mem *pgt, u32 index, struct nouveau_vma *vma, struct nouveau_mem *mem, dma_addr_t *list, u32 count) {
	struct drm_device* dev = pgt->dev;
	struct nouveau_para_virt_slot* slot = nouveau_para_virt_alloc_slot(dev);
	const u32 filled = count / NOUVEAU_PV_BATCH_SIZE;
	const u32 rest = count % NOUVEAU_PV_BATCH_SIZE;
	const u32 target = (vma->access & NV_MEM_ACCESS_NOSNOOP) ? 7 : 5;

	if (filled) {
		int ret;
		u32 i, j;
		for (i = 0; i < filled; ++i) {
			slot->u8[0] = NOUVEAU_PV_OP_MAP_SG_BATCH;
			slot->u32[1] = pgt->id;
			slot->u32[2] = index + NOUVEAU_PV_BATCH_SIZE * i;
			slot->u32[3] = NOUVEAU_PV_BATCH_SIZE;
			for (j = 0; j < NOUVEAU_PV_BATCH_SIZE; ++j) {
				slot->u64[2 + j] = nvc0_vm_addr(vma, *list++, mem->memtype, target);
			}
			nouveau_para_virt_call(dev, slot);
			ret = slot->u32[0];
			if (ret) {
				nouveau_para_virt_free_slot(dev, slot);
				return ret;
			}
		}
	}

	if (rest) {
		int ret;
		u32 i;
		slot->u8[0] = NOUVEAU_PV_OP_MAP_SG_BATCH;
		slot->u32[1] = pgt->id;
		slot->u32[2] = index + NOUVEAU_PV_BATCH_SIZE * filled;
		slot->u32[3] = rest;
		for (i = 0; i < rest; ++i) {
			slot->u64[2 + i] = nvc0_vm_addr(vma, *list++, mem->memtype, target);
		}
		nouveau_para_virt_call(dev, slot);
		ret = slot->u32[0];
		if (ret) {
			nouveau_para_virt_free_slot(dev, slot);
			return ret;
		}
	}

	nouveau_para_virt_free_slot(dev, slot);
	return 0;
}

int nouveau_para_virt_unmap_batch(struct nouveau_para_virt_mem *pgt, u32 index, u32 count) {
	struct drm_device* dev = pgt->dev;
	int ret;
	struct nouveau_para_virt_slot* slot = nouveau_para_virt_alloc_slot(dev);
	slot->u8[0] = NOUVEAU_PV_OP_UNMAP_BATCH;
	slot->u32[1] = pgt->id;
	slot->u32[2] = index;
	slot->u32[3] = count;
	nouveau_para_virt_call(dev, slot);

	ret = slot->u32[0];

	nouveau_para_virt_free_slot(dev, slot);
	return ret;
}

int nouveau_para_virt_vm_flush(struct nouveau_para_virt_mem *pgd, u32 engine) {
	struct drm_device* dev = pgd->dev;
	int ret;
	struct nouveau_para_virt_slot* slot = nouveau_para_virt_alloc_slot(dev);
	slot->u8[0] = NOUVEAU_PV_OP_VM_FLUSH;
	slot->u32[1] = pgd->id;
	slot->u32[2] = engine;
	nouveau_para_virt_call(dev, slot);

	ret = slot->u32[0];

	nouveau_para_virt_free_slot(dev, slot);

	return ret;
}

int nouveau_para_virt_bar3_pgt(struct nouveau_para_virt_mem *pgt) {
	struct drm_device* dev = pgt->dev;
	int ret;
	struct nouveau_para_virt_slot* slot = nouveau_para_virt_alloc_slot(dev);
	slot->u8[0] = NOUVEAU_PV_OP_BAR3_PGT;
	slot->u32[1] = pgt->id;
	nouveau_para_virt_call(dev, slot);

	ret = slot->u32[0];

	nouveau_para_virt_free_slot(dev, slot);

	return ret;
}
