/*
 * Copyright (C) 2007 Ben Skeggs.
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "drm.h"

#include "nouveau_drv.h"
#include "nouveau_vm.h"

struct nv50_instmem_priv {
	uint32_t save1700[5]; /* 0x1700->0x1710 */

	struct nouveau_gpuobj *bar1_dmaobj;
	struct nouveau_gpuobj *bar3_dmaobj;
};

struct nv50_gpuobj_node {
	struct nouveau_mem *vram;
	struct nouveau_vma chan_vma;
	u32 align;
};

int
nv50_instmem_get(struct nouveau_gpuobj *gpuobj, struct nouveau_channel *chan,
		 u32 size, u32 align)
{
	struct drm_device *dev = gpuobj->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_vram_engine *vram = &dev_priv->engine.vram;
	struct nv50_gpuobj_node *node = NULL;
	int ret;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	node->align = align;

	size  = (size + 4095) & ~4095;
	align = max(align, (u32)4096);

	ret = vram->get(dev, size, align, 0, 0, &node->vram);
	if (ret) {
		kfree(node);
		return ret;
	}

	gpuobj->vinst = node->vram->offset;

	if (gpuobj->flags & NVOBJ_FLAG_VM) {
		u32 flags = NV_MEM_ACCESS_RW;
		if (!(gpuobj->flags & NVOBJ_FLAG_VM_USER))
			flags |= NV_MEM_ACCESS_SYS;

		ret = nouveau_vm_get(chan->vm, size, 12, flags,
				     &node->chan_vma);
		if (ret) {
			vram->put(dev, &node->vram);
			kfree(node);
			return ret;
		}

		nouveau_vm_map(&node->chan_vma, node->vram);
		gpuobj->linst = node->chan_vma.offset;
	}

	gpuobj->size = size;
	gpuobj->node = node;
	return 0;
}

void
nv50_instmem_put(struct nouveau_gpuobj *gpuobj)
{
	struct drm_device *dev = gpuobj->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_vram_engine *vram = &dev_priv->engine.vram;
	struct nv50_gpuobj_node *node;

	node = gpuobj->node;
	gpuobj->node = NULL;

	if (node->chan_vma.node) {
		nouveau_vm_unmap(&node->chan_vma);
		nouveau_vm_put(&node->chan_vma);
	}
	vram->put(dev, &node->vram);
	kfree(node);
}

int
nv50_instmem_map(struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = gpuobj->dev->dev_private;
	struct nv50_gpuobj_node *node = gpuobj->node;
	int ret;

	ret = nouveau_vm_get(dev_priv->bar3_vm, gpuobj->size, 12,
			     NV_MEM_ACCESS_RW, &node->vram->bar_vma);
	if (ret)
		return ret;

	nouveau_vm_map(&node->vram->bar_vma, node->vram);
	gpuobj->pinst = node->vram->bar_vma.offset;
	return 0;
}

void
nv50_instmem_unmap(struct nouveau_gpuobj *gpuobj)
{
	struct nv50_gpuobj_node *node = gpuobj->node;

	if (node->vram->bar_vma.node) {
		nouveau_vm_unmap(&node->vram->bar_vma);
		nouveau_vm_put(&node->vram->bar_vma);
	}
}

void
nv84_instmem_flush(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->vm_lock, flags);
	nv_wr32(dev, 0x070000, 0x00000001);
	if (!nv_wait(dev, 0x070000, 0x00000002, 0x00000000))
		NV_ERROR(dev, "PRAMIN flush timeout\n");
	spin_unlock_irqrestore(&dev_priv->vm_lock, flags);
}

