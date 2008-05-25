/*
 * Copyright 2007 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Sun designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Sun in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 */

/* @test 
   @summary Test SoftLimiter processAudio method */

import javax.sound.midi.MidiUnavailableException;
import javax.sound.midi.Patch;
import javax.sound.sampled.*;

import com.sun.media.sound.*;

public class ProcessAudio_replace_mix_overdrive {
	
	private static void assertEquals(Object a, Object b) throws Exception
	{
		if(!a.equals(b))
			throw new RuntimeException("assertEquals fails!");
	}	
	
	private static void assertTrue(boolean value) throws Exception
	{
		if(!value)
			throw new RuntimeException("assertTrue fails!");
	}
		
	public static void main(String[] args) throws Exception {
		SoftSynthesizer synth = new SoftSynthesizer();
		synth.openStream(null, null);
		
		SoftAudioBuffer in1 = new SoftAudioBuffer(250, synth.getFormat());
		SoftAudioBuffer in2 = new SoftAudioBuffer(250, synth.getFormat());
		SoftAudioBuffer out1 = new SoftAudioBuffer(250, synth.getFormat());
		SoftAudioBuffer out2 = new SoftAudioBuffer(250, synth.getFormat());
		
		float[] testdata1 = new float[in1.getSize()];
		float[] testdata2 = new float[in2.getSize()];
		float[] n1a = in1.array();
		float[] n2a = in2.array();
		float[] out1a = out1.array();
		float[] out2a = out2.array();
		for (int i = 0; i < n1a.length; i++) {
			testdata1[i] = (float)Math.sin(i*0.3)*2.5f;
			testdata2[i] = (float)Math.sin(i*0.4)*2.5f;
			n1a[i] = testdata1[i];
			n2a[i] = testdata2[i];
			out1a[i] = 1;
			out2a[i] = 1;
		}
		
		SoftLimiter limiter = new SoftLimiter();
		limiter.init(synth.getFormat().getSampleRate(),
			     synth.getControlRate());
		limiter.setMixMode(true);
		limiter.setInput(0, in1);
		limiter.setInput(1, in2);
		limiter.setOutput(0, out1);
		limiter.setOutput(1, out2);
		limiter.processControlLogic();
		limiter.processAudio();
		limiter.processControlLogic();
		limiter.processAudio();
		// Limiter should delay audio by one buffer,
		// and there should almost no different in output v.s. input
		for (int i = 0; i < n1a.length; i++) {
			if(Math.abs(out1a[i]-1) > 1.0)
				throw new Exception("abs(output)>1");
			if(Math.abs(out2a[i]-1) > 1.0)
				throw new Exception("abs(output)>1");
		}
		
		synth.close();
	}
}
