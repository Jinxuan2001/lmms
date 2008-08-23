/*
 * bit_invader.cpp - instrument which uses a usereditable wavetable
 *
 * Copyright (c) 2006-2008 Andreas Brandmaier <andy/at/brandmaier/dot/de>
 * 
 * This file is part of Linux MultiMedia Studio - http://lmms.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */


#include "bit_invader.h"


#include <QtXml/QDomElement>


using namespace std;


#include "engine.h"
#include "graph.h"
#include "instrument_track.h"
#include "knob.h"
#include "led_checkbox.h"
#include "note_play_handle.h"
#include "oscillator.h"
#include "pixmap_button.h"
#include "song_editor.h"
#include "templates.h"
#include "tooltip.h"
#include "song.h"

#undef SINGLE_SOURCE_COMPILE
#include "embed.cpp"

extern "C"
{

plugin::descriptor PLUGIN_EXPORT bitinvader_plugin_descriptor =
{
	STRINGIFY_PLUGIN_NAME( PLUGIN_NAME ),
	"BitInvader",
	QT_TRANSLATE_NOOP( "pluginBrowser",
				"Rough & Dirty Wavetable Synthesizer." ),
	"Andreas Brandmaier <andreas/at/brandmaier/dot/de>",
	0x0100,
	plugin::Instrument,
	new pluginPixmapLoader( "logo" ),
	NULL
} ;

}


bSynth::bSynth( float * shape, int length, float _pitch, bool _interpolation,
				float factor, const sample_rate_t _sample_rate )
{

	interpolation = _interpolation;

	// init variables

	sample_length = length;
	sample_shape = new float[sample_length];
	for (int i=0; i < length; i++)
	{
		sample_shape[i] = shape[i] * factor;
	}


	sample_index = 0;
	sample_realindex = 0;
	

	sample_step = static_cast<float>( sample_length / ( _sample_rate /
		 						_pitch ) );
	

}


bSynth::~bSynth()
{
	delete[] sample_shape;
}

sample_t bSynth::nextStringSample( void )
{

	
	// check overflow
	while (sample_realindex >= sample_length) {
		sample_realindex -= sample_length;
	}

	sample_t sample;

	if (interpolation) {

		// find position in shape 
		int a = static_cast<int>(sample_realindex);	
		int b;
		if (a < (sample_length-1)) {
			b = static_cast<int>(sample_realindex+1);
		} else {
			b = 0;
		}
		
		// Nachkommaanteil
		float frac = sample_realindex - static_cast<int>(sample_realindex);
		
		sample = sample_shape[a]*(1-frac) + sample_shape[b]*(frac);

	} else {
		// No interpolation
		sample_index = static_cast<int>(sample_realindex);	
		sample = sample_shape[sample_index];
	}
	
	// progress in shape
	sample_realindex += sample_step;

	return sample;
}	

/***********************************************************************
*
*	class BitInvader
*
*	lmms - plugin 
*
***********************************************************************/


bitInvader::bitInvader( instrumentTrack * _channel_track ) :
	instrument( _channel_track, &bitinvader_plugin_descriptor ),
	m_sampleLength( 128, 8, 128, 1, this, tr( "Samplelength" ) ),
	m_graph( -1.0f, 1.0f, 128, this ),
	m_interpolation( FALSE, this ),
	m_normalize( FALSE, this)
{

	m_graph.setWaveToSine();

	connect( &m_sampleLength, SIGNAL( dataChanged( ) ),
			this, SLOT( lengthChanged( ) )
			);

	connect( &m_graph, SIGNAL( samplesChanged( int, int ) ),
			this, SLOT( samplesChanged( int, int ) ) );

}


bitInvader::~bitInvader()
{
}




void bitInvader::saveSettings( QDomDocument & _doc, QDomElement & _this )
{

	// Save plugin version
	_this.setAttribute( "version", "0.1" );

	// Save sample length
	m_sampleLength.saveSettings( _doc, _this, "sampleLength" );

	// Save sample shape base64-encoded
	QString sampleString;
	base64::encode( (const char *)m_graph.samples(),
		m_graph.length() * sizeof(float), sampleString );
	_this.setAttribute( "sampleShape", sampleString );
	

	// save LED normalize 
	m_interpolation.saveSettings( _doc, _this, "interpolation" );
	
	// save LED 
	m_normalize.saveSettings( _doc, _this, "normalize" );
}




void bitInvader::loadSettings( const QDomElement & _this )
{
	// Load sample length
	m_sampleLength.loadSettings( _this, "sampleLength" );

	int sampleLength = (int)m_sampleLength.value();

	// Load sample shape
	int size = 0;
	char * dst = 0;
	base64::decode( _this.attribute( "sampleShape"), &dst, &size );

	m_graph.setLength( sampleLength );
	m_graph.setSamples( (float*) dst );
	delete[] dst;

	// Load LED normalize 
	m_interpolation.loadSettings( _this, "interpolation" );
	// Load LED 
	m_normalize.loadSettings( _this, "normalize" );

//	songEditor::inst()->setModified();

}


void bitInvader::lengthChanged( void )
{
	m_graph.setLength( m_sampleLength.value() );

	normalize();
}


void bitInvader::samplesChanged( int _begin, int _end )
{
	normalize();
	//engine::getSongEditor()->setModified();
}


void bitInvader::normalize( void )
{
	// analyze
	float max = 0;
	const float* samples = m_graph.samples();
	for(int i=0; i < m_graph.length(); i++)
	{
		if (fabsf(samples[i]) > max) { max = fabs(samples[i]); }
	}
	normalizeFactor = 1.0 / max;
}



QString bitInvader::nodeName( void ) const
{
	return( bitinvader_plugin_descriptor.name );
}


void bitInvader::playNote( notePlayHandle * _n, bool,
						sampleFrame * _working_buffer )
{
	if ( _n->totalFramesPlayed() == 0 || _n->m_pluginData == NULL )
	{
	
		float factor;
		if( !m_normalize.value() )
		{
			factor = 1.0f;
		}
		else
		{
			factor = normalizeFactor;
		}

		_n->m_pluginData = new bSynth(
					const_cast<float*>( m_graph.samples() ),
					m_graph.length(),
					_n->frequency(),
					m_interpolation.value(), factor,
				engine::getMixer()->processingSampleRate() );
	}

	const fpp_t frames = _n->framesLeftForCurrentPeriod();

	bSynth * ps = static_cast<bSynth *>( _n->m_pluginData );
	for( fpp_t frame = 0; frame < frames; ++frame )
	{
		const sample_t cur = ps->nextStringSample();
		for( Uint8 chnl = 0; chnl < DEFAULT_CHANNELS; ++chnl )
		{
			_working_buffer[frame][chnl] = cur;
		}
	}

	applyRelease( _working_buffer, _n );

	getInstrumentTrack()->processAudioBuffer( _working_buffer, frames, _n );
}


void bitInvader::deleteNotePluginData( notePlayHandle * _n )
{
	delete static_cast<bSynth *>( _n->m_pluginData );
}


pluginView * bitInvader::instantiateView( QWidget * _parent )
{
	return( new bitInvaderView( this, _parent ) );
}


bitInvaderView::bitInvaderView( instrument * _instrument,
					QWidget * _parent ) :
	instrumentView( _instrument, _parent )
{
	setAutoFillBackground( TRUE );
	QPalette pal;

	pal.setBrush( backgroundRole(), PLUGIN_NAME::getIconPixmap(
								"artwork" ) );
	setPalette( pal );
	
	m_sampleLengthKnob = new knob( knobDark_28, this );
	m_sampleLengthKnob->move( 10, 120 );
	m_sampleLengthKnob->setHintText( tr( "Sample Length" ) + " ", "" );

	m_graph = new graph( this, graph::NearestStyle );
	m_graph->move(53,118);	// 55,120 - 2px border
	m_graph->setAutoFillBackground( TRUE );

	toolTip::add( m_graph, tr ( "Draw your own waveform here "
				"by dragging your mouse on this graph."
	));


	pal = QPalette();
	pal.setBrush( backgroundRole(), 
			PLUGIN_NAME::getIconPixmap("wavegraph3") );
	m_graph->setPalette( pal );


	sinWaveBtn = new pixmapButton( this, tr( "Sine wave" ) );
	sinWaveBtn->move( 188, 120 );
	sinWaveBtn->setActiveGraphic( embed::getIconPixmap(
						"sin_wave_active" ) );
	sinWaveBtn->setInactiveGraphic( embed::getIconPixmap(
						"sin_wave_inactive" ) );
	toolTip::add( sinWaveBtn,
			tr( "Click for a sine-wave." ) );

	triangleWaveBtn = new pixmapButton( this, tr( "Triangle wave" ) );
	triangleWaveBtn->move( 188, 136 );
	triangleWaveBtn->setActiveGraphic(
		embed::getIconPixmap( "triangle_wave_active" ) );
	triangleWaveBtn->setInactiveGraphic(
		embed::getIconPixmap( "triangle_wave_inactive" ) );
	toolTip::add( triangleWaveBtn,
			tr( "Click here for a triangle-wave." ) );

	sawWaveBtn = new pixmapButton( this, tr( "Saw wave" ) );
	sawWaveBtn->move( 188, 152 );
	sawWaveBtn->setActiveGraphic( embed::getIconPixmap(
						"saw_wave_active" ) );
	sawWaveBtn->setInactiveGraphic( embed::getIconPixmap(
						"saw_wave_inactive" ) );
	toolTip::add( sawWaveBtn,
			tr( "Click here for a saw-wave." ) );

	sqrWaveBtn = new pixmapButton( this, tr( "Square wave" ) );
	sqrWaveBtn->move( 188, 168 );
	sqrWaveBtn->setActiveGraphic( embed::getIconPixmap(
					"square_wave_active" ) );
	sqrWaveBtn->setInactiveGraphic( embed::getIconPixmap(
					"square_wave_inactive" ) );
	toolTip::add( sqrWaveBtn,
			tr( "Click here for a square-wave." ) );

	whiteNoiseWaveBtn = new pixmapButton( this,
					tr( "White noise wave" ) );
	whiteNoiseWaveBtn->move( 188, 184 );
	whiteNoiseWaveBtn->setActiveGraphic(
		embed::getIconPixmap( "white_noise_wave_active" ) );
	whiteNoiseWaveBtn->setInactiveGraphic(
		embed::getIconPixmap( "white_noise_wave_inactive" ) );
	toolTip::add( whiteNoiseWaveBtn,
			tr( "Click here for white-noise." ) );

	usrWaveBtn = new pixmapButton( this, tr( "User defined wave" ) );
	usrWaveBtn->move( 188, 200 );
	usrWaveBtn->setActiveGraphic( embed::getIconPixmap(
						"usr_wave_active" ) );
	usrWaveBtn->setInactiveGraphic( embed::getIconPixmap(
						"usr_wave_inactive" ) );
	toolTip::add( usrWaveBtn,
			tr( "Click here for a user-defined shape." ) );

	smoothBtn = new pixmapButton( this, tr( "Smooth" ) );
	smoothBtn->move( 55, 225 );
	smoothBtn->setActiveGraphic( PLUGIN_NAME::getIconPixmap(
						"smooth" ) );
	smoothBtn->setInactiveGraphic( PLUGIN_NAME::getIconPixmap(
						"smooth" ) );
	smoothBtn->setChecked( TRUE );
	toolTip::add( smoothBtn,
			tr( "Click here to smooth waveform." ) );


	m_interpolationToggle = new ledCheckBox( "Interpolation", this,
							tr( "Interpolation" ) );
	m_interpolationToggle->move( 55,80 );


	m_normalizeToggle = new ledCheckBox( "Normalize", this,
							tr( "Normalize" ) );
	m_normalizeToggle->move( 55, 100 );
	
	
	connect( sinWaveBtn, SIGNAL (clicked ( void ) ),
			this, SLOT ( sinWaveClicked( void ) ) );
	connect( triangleWaveBtn, SIGNAL ( clicked ( void ) ),
			this, SLOT ( triangleWaveClicked( void ) ) );
	connect( sawWaveBtn, SIGNAL (clicked ( void ) ),
			this, SLOT ( sawWaveClicked( void ) ) );
	connect( sqrWaveBtn, SIGNAL ( clicked ( void ) ),
			this, SLOT ( sqrWaveClicked( void ) ) );
	connect( whiteNoiseWaveBtn, SIGNAL ( clicked ( void ) ),
			this, SLOT ( noiseWaveClicked( void ) ) );
	connect( usrWaveBtn, SIGNAL ( clicked ( void ) ),
			this, SLOT ( usrWaveClicked( void ) ) );
	
	connect( smoothBtn, SIGNAL ( clicked ( void ) ),
			this, SLOT ( smoothClicked( void ) ) );		

	connect( m_interpolationToggle, SIGNAL( toggled( bool ) ),
			this, SLOT ( interpolationToggled( bool ) ) );

	connect( m_normalizeToggle, SIGNAL( toggled( bool ) ),
			this, SLOT ( normalizeToggled( bool ) ) );

}

void bitInvaderView::modelChanged( void )
{
	bitInvader * b = castModel<bitInvader>();

	m_graph->setModel( &b->m_graph );
	m_sampleLengthKnob->setModel( &b->m_sampleLength );
	m_interpolationToggle->setModel( &b->m_interpolation );
	m_normalizeToggle->setModel( &b->m_normalize );

}


void bitInvaderView::sinWaveClicked( void )
{
	m_graph->model()->setWaveToSine();
	engine::getSong()->setModified();
}

void bitInvaderView::triangleWaveClicked( void )
{
	m_graph->model()->setWaveToTriangle();
	engine::getSong()->setModified();
}


void bitInvaderView::sawWaveClicked( void )
{
	m_graph->model()->setWaveToSaw();
	engine::getSong()->setModified();
}

void bitInvaderView::sqrWaveClicked( void )
{
	m_graph->model()->setWaveToSquare();
	engine::getSong()->setModified();
}

void bitInvaderView::noiseWaveClicked( void )
{
	m_graph->model()->setWaveToNoise();
	engine::getSong()->setModified();
}

void bitInvaderView::usrWaveClicked( void )
{
	/*
	m_graph->model()->setWaveToNoise();
	engine::getSong()->setModified();
	// zero sample_shape
	for (int i = 0; i < sample_length; i++)
	{
		sample_shape[i] = 0;
	}

	// load user shape
	sampleBuffer buffer;
	QString af = buffer.openAudioFile();
	if ( af != "" )
	{
		buffer.setAudioFile( af );
		
		// copy buffer data
		sample_length = min( sample_length, static_cast<int>(
							buffer.frames() ) );
		for ( int i = 0; i < sample_length; i++ )
		{
			sample_shape[i] = (float)*buffer.data()[i];
		}
	}

	sampleChanged();
	*/
}


void bitInvaderView::smoothClicked( void )
{
	m_graph->model()->smooth();
	engine::getSong()->setModified();
}


void bitInvaderView::interpolationToggled( bool value )
{
	m_graph->setGraphStyle( value ? graph::LinearStyle : graph::NearestStyle);
	engine::getSong()->setModified();
}


void bitInvaderView::normalizeToggled( bool value )
{
	engine::getSong()->setModified();
}


extern "C"
{

// neccessary for getting instance out of shared lib
plugin * PLUGIN_EXPORT lmms_plugin_main( model *, void * _data )
{
	return( new bitInvader( static_cast<instrumentTrack *>( _data ) ) );
}


}



#include "moc_bit_invader.cxx"
