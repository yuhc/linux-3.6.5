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

#define NOUVEAU_PARA_VIRT_REG_BAR 4
#define NOUVEAU_PARA_VIRT_SLOT_SIZE 0x100ULL
#define NOUVEAU_PARA_VIRT_SLOT_NUM 64ULL
#define NOUVEAU_PARA_VIRT_SLOT_TOTAL (NOUVEAU_PARA_VIRT_SLOT_SIZE * NOUVEAU_PARA_VIRT_SLOT_NUM)

struct nouveau_para_virt_priv {
	struct drm_device *dev;
	spinlock_t slot_lock;
	uint8_t __iomem *slot;
	uint8_t __iomem *mmio;
	uint64_t used_slot;
	struct semaphore sema;
};

static inline u32 nvpv_rd32(struct nouveau_para_virt_engine *engine, unsigned reg) {
	struct nouveau_para_virt_priv *priv = engine->priv;
	return ioread32_native(priv->mmio + reg);
}

static inline void nvpv_wr32(struct nouveau_para_virt_engine *engine, unsigned reg, u32 val) {
	struct nouveau_para_virt_priv *priv = engine->priv;
	iowrite32_native(val, priv->mmio + reg);
}

static inline u32 slot_pos(struct nouveau_para_virt_priv *priv, u8 *slot) {
	return (slot - priv->slot) / NOUVEAU_PARA_VIRT_SLOT_SIZE;
}

u8* nouveau_para_virt_alloc_slot(struct drm_device *dev) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_para_virt_engine *engine = &dev_priv->engine.para_virt;
	struct nouveau_para_virt_priv *priv = engine->priv;

	unsigned long pos;
	unsigned long flags;
	u8* ret = NULL;

	down(&priv->sema);
	spin_lock_irqsave(&priv->slot_lock, flags);

	pos = fls64(priv->used_slot);
	BUG_ON(pos == 0);

	pos -= 1;
	priv->used_slot |= ((0x1ULL) << pos);
	ret = priv->slot + pos * NOUVEAU_PARA_VIRT_SLOT_SIZE;

	spin_unlock_irqrestore(&priv->slot_lock, flags);

	return ret;
}

void nouveau_para_virt_free_slot(struct drm_device *dev, u8 *slot) {
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_para_virt_engine *engine = &dev_priv->engine.para_virt;
	struct nouveau_para_virt_priv *priv = engine->priv;
	unsigned long pos = slot_pos(priv, slot);

	spin_lock_irqsave(&priv->slot_lock, flags);
	priv->used_slot &= ~((0x1ULL) << pos);
	spin_unlock_irqrestore(&priv->slot_lock, flags);
	up(&priv->sema);
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
	sema_init(&priv->sema, NOUVEAU_PARA_VIRT_SLOT_NUM);

	if (!(priv->slot = kzalloc(NOUVEAU_PARA_VIRT_SLOT_TOTAL, GFP_KERNEL))) {
		return -ENOMEM;
	}
	((uint32_t*)priv->slot)[0] = 0xdeadbeefUL;

	// map BAR4
	priv->mmio = (uint8_t __iomem *)ioremap(pci_resource_start(pdev, NOUVEAU_PARA_VIRT_REG_BAR), pci_resource_len(pdev, NOUVEAU_PARA_VIRT_REG_BAR));
	if (!priv->mmio) {
		return -ENODEV;
	}

	// notify this physical address to A3
	address = __pa(priv->slot);  // convert kmalloc-ed virt to phys
	nvpv_wr32(engine, 0x4, lower_32_bits(address));
	nvpv_wr32(engine, 0x8, upper_32_bits(address));
	if (nvpv_rd32(engine, 0x0) != 0x0) {
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

