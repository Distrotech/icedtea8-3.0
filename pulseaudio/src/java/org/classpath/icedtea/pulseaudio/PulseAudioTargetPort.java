/* PulseAudioClip.java
   Copyright (C) 2008 Red Hat, Inc.

This file is part of IcedTea.

IcedTea is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 2.

IcedTea is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with IcedTea; see the file COPYING.  If not, write to
the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301 USA.

Linking this library statically or dynamically with other modules is
making a combined work based on this library.  Thus, the terms and
conditions of the GNU General Public License cover the whole
combination.

As a special exception, the copyright holders of this library give you
permission to link this library with independent modules to produce an
executable, regardless of the license terms of these independent
modules, and to copy and distribute the resulting executable under
terms of your choice, provided that you also meet, for each linked
independent module, the terms and conditions of the license of that
module.  An independent module is a module which is not derived from
or based on this library.  If you modify this library, you may extend
this exception to your version of the library, but you are not
obligated to do so.  If you do not wish to do so, delete this
exception statement from your version.
 */

package org.classpath.icedtea.pulseaudio;

import javax.sound.sampled.Port;

public class PulseAudioTargetPort extends PulseAudioPort {

	/* aka speaker */

	static {
		SecurityWrapper.loadNativeLibrary();
	}

	public PulseAudioTargetPort(String name) {

		super(name);
	}

	public void open() {

		super.open();

		PulseAudioMixer parent = PulseAudioMixer.getInstance();
		parent.addTargetLine(this);
	}

	public void close() {

		if (!isOpen) {
			throw new IllegalStateException("not open, so cant close Port");
		}

		PulseAudioMixer parent = PulseAudioMixer.getInstance();
		parent.removeTargetLine(this);

		super.close();
	}

	public native byte[] native_setVolume(float newValue);

	public synchronized native byte[] native_updateVolumeInfo();

	@Override
	public javax.sound.sampled.Line.Info getLineInfo() {
		return new Port.Info(Port.class, getName(), false);
	}

}
