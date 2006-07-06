/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *
 * gstbaseaudiosrc.c: 
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>

#include "gstbaseaudiosrc.h"

GST_DEBUG_CATEGORY_STATIC (gst_base_audio_src_debug);
#define GST_CAT_DEFAULT gst_base_audio_src_debug

/* BaseAudioSrc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_BUFFER_TIME     ((200 * GST_MSECOND) / GST_USECOND)
#define DEFAULT_LATENCY_TIME    ((10 * GST_MSECOND) / GST_USECOND)

enum
{
  PROP_0,
  PROP_BUFFER_TIME,
  PROP_LATENCY_TIME,
};

#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_base_audio_src_debug, "baseaudiosrc", 0, "baseaudiosrc element");

GST_BOILERPLATE_FULL (GstBaseAudioSrc, gst_base_audio_src, GstPushSrc,
    GST_TYPE_PUSH_SRC, _do_init);

static void gst_base_audio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_base_audio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_base_audio_src_fixate (GstPad * pad, GstCaps * caps);

static GstStateChangeReturn gst_base_audio_src_change_state (GstElement *
    element, GstStateChange transition);

static GstClock *gst_base_audio_src_provide_clock (GstElement * elem);
static gboolean gst_base_audio_src_set_clock (GstElement * elem,
    GstClock * clock);
static GstClockTime gst_base_audio_src_get_time (GstClock * clock,
    GstBaseAudioSrc * src);

static GstFlowReturn gst_base_audio_src_create (GstBaseSrc * bsrc,
    guint64 offset, guint length, GstBuffer ** buf);
static gboolean gst_base_audio_src_check_get_range (GstBaseSrc * bsrc);

static gboolean gst_base_audio_src_event (GstBaseSrc * bsrc, GstEvent * event);
static void gst_base_audio_src_get_times (GstBaseSrc * bsrc,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static gboolean gst_base_audio_src_setcaps (GstBaseSrc * bsrc, GstCaps * caps);

//static guint gst_base_audio_src_signals[LAST_SIGNAL] = { 0 };

static void
gst_base_audio_src_base_init (gpointer g_class)
{
}

static void
gst_base_audio_src_class_init (GstBaseAudioSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_base_audio_src_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_base_audio_src_get_property);

  g_object_class_install_property (gobject_class, PROP_BUFFER_TIME,
      g_param_spec_int64 ("buffer-time", "Buffer Time",
          "Size of audio buffer in microseconds", 1,
          G_MAXINT64, DEFAULT_BUFFER_TIME, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_LATENCY_TIME,
      g_param_spec_int64 ("latency-time", "Latency Time",
          "Audio latency in microseconds", 1,
          G_MAXINT64, DEFAULT_LATENCY_TIME, G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_audio_src_change_state);
  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_base_audio_src_provide_clock);
  gstelement_class->set_clock =
      GST_DEBUG_FUNCPTR (gst_base_audio_src_set_clock);

  gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_base_audio_src_setcaps);
  gstbasesrc_class->event = GST_DEBUG_FUNCPTR (gst_base_audio_src_event);
  gstbasesrc_class->get_times =
      GST_DEBUG_FUNCPTR (gst_base_audio_src_get_times);
  gstbasesrc_class->create = GST_DEBUG_FUNCPTR (gst_base_audio_src_create);
  gstbasesrc_class->check_get_range =
      GST_DEBUG_FUNCPTR (gst_base_audio_src_check_get_range);
}

static void
gst_base_audio_src_init (GstBaseAudioSrc * baseaudiosrc,
    GstBaseAudioSrcClass * g_class)
{
  baseaudiosrc->buffer_time = DEFAULT_BUFFER_TIME;
  baseaudiosrc->latency_time = DEFAULT_LATENCY_TIME;
  /* reset blocksize we use latency time to calculate a more useful 
   * value based on negotiated format. */
  GST_BASE_SRC (baseaudiosrc)->blocksize = 0;

  baseaudiosrc->clock = gst_audio_clock_new ("clock",
      (GstAudioClockGetTimeFunc) gst_base_audio_src_get_time, baseaudiosrc);

  gst_pad_set_fixatecaps_function (GST_BASE_SRC_PAD (baseaudiosrc),
      gst_base_audio_src_fixate);

  /* we are always a live source */
  gst_base_src_set_live (GST_BASE_SRC (baseaudiosrc), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (baseaudiosrc), GST_FORMAT_TIME);
}

static gboolean
gst_base_audio_src_set_clock (GstElement * elem, GstClock * clock)
{
  GstBaseAudioSrc *src;

  src = GST_BASE_AUDIO_SRC (elem);

  /* FIXME, we cannot slave to another clock yet, better fail 
   * than to give a bad user experience (tm). */
  if (clock && clock != src->clock)
    goto wrong_clock;

  return TRUE;

  /* ERRORS */
wrong_clock:
  {
    GST_ELEMENT_ERROR (src, CORE, CLOCK,
        (NULL), ("Cannot operate with this clock."));
    return FALSE;
  }
}

static GstClock *
gst_base_audio_src_provide_clock (GstElement * elem)
{
  GstBaseAudioSrc *src;
  GstClock *clock;

  src = GST_BASE_AUDIO_SRC (elem);

  /* we have no ringbuffer (must be NULL state) */
  if (src->ringbuffer == NULL)
    goto wrong_state;

  if (!gst_ring_buffer_is_acquired (src->ringbuffer))
    goto wrong_state;

  clock = GST_CLOCK_CAST (gst_object_ref (src->clock));

  return clock;

  /* ERRORS */
wrong_state:
  {
    GST_DEBUG_OBJECT (src, "ringbuffer not acquired");
    return NULL;
  }
}

static GstClockTime
gst_base_audio_src_get_time (GstClock * clock, GstBaseAudioSrc * src)
{
  guint64 samples;
  GstClockTime result;

  if (G_UNLIKELY (src->ringbuffer == NULL || src->ringbuffer->spec.rate == 0))
    return GST_CLOCK_TIME_NONE;

  samples = gst_ring_buffer_samples_done (src->ringbuffer);

  result = gst_util_uint64_scale_int (samples, GST_SECOND,
      src->ringbuffer->spec.rate);

  return result;
}

static gboolean
gst_base_audio_src_check_get_range (GstBaseSrc * bsrc)
{
  /* we allow limited pull base operation of which the details
   * will eventually exposed in an as of yet non-existing query.
   * Basically pulling can be done on any number of bytes as long
   * as the offset is -1 or sequentially increasing. */
  return TRUE;
}

static void
gst_base_audio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseAudioSrc *src;

  src = GST_BASE_AUDIO_SRC (object);

  switch (prop_id) {
    case PROP_BUFFER_TIME:
      src->buffer_time = g_value_get_int64 (value);
      break;
    case PROP_LATENCY_TIME:
      src->latency_time = g_value_get_int64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_audio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseAudioSrc *src;

  src = GST_BASE_AUDIO_SRC (object);

  switch (prop_id) {
    case PROP_BUFFER_TIME:
      g_value_set_int64 (value, src->buffer_time);
      break;
    case PROP_LATENCY_TIME:
      g_value_set_int64 (value, src->latency_time);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_audio_src_fixate (GstPad * pad, GstCaps * caps)
{
  GstStructure *s;

  s = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (s, "rate", 44100);
  gst_structure_fixate_field_nearest_int (s, "channels", 2);
  gst_structure_fixate_field_nearest_int (s, "depth", 16);
  gst_structure_fixate_field_nearest_int (s, "width", 16);
  gst_structure_set (s, "signed", G_TYPE_BOOLEAN, TRUE, NULL);
  if (gst_structure_has_field (s, "endianness"))
    gst_structure_fixate_field_nearest_int (s, "endianness", G_BYTE_ORDER);
}

static gboolean
gst_base_audio_src_setcaps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstBaseAudioSrc *src = GST_BASE_AUDIO_SRC (bsrc);
  GstRingBufferSpec *spec;

  spec = &src->ringbuffer->spec;

  spec->buffer_time = src->buffer_time;
  spec->latency_time = src->latency_time;

  if (!gst_ring_buffer_parse_caps (spec, caps))
    goto parse_error;

  /* calculate suggested segsize and segtotal */
  spec->segsize =
      spec->rate * spec->bytes_per_sample * spec->latency_time / GST_MSECOND;
  spec->segtotal = spec->buffer_time / spec->latency_time;

  GST_DEBUG ("release old ringbuffer");

  gst_ring_buffer_release (src->ringbuffer);

  gst_ring_buffer_debug_spec_buff (spec);

  GST_DEBUG ("acquire new ringbuffer");

  if (!gst_ring_buffer_acquire (src->ringbuffer, spec))
    goto acquire_error;

  /* calculate actual latency and buffer times */
  spec->latency_time =
      spec->segsize * GST_MSECOND / (spec->rate * spec->bytes_per_sample);
  spec->buffer_time =
      spec->segtotal * spec->segsize * GST_MSECOND / (spec->rate *
      spec->bytes_per_sample);

  gst_ring_buffer_debug_spec_buff (spec);

  return TRUE;

  /* ERRORS */
parse_error:
  {
    GST_DEBUG ("could not parse caps");
    return FALSE;
  }
acquire_error:
  {
    GST_DEBUG ("could not acquire ringbuffer");
    return FALSE;
  }
}

static void
gst_base_audio_src_get_times (GstBaseSrc * bsrc, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* ne need to sync to a clock here, we schedule the samples based
   * on our own clock for the moment. FIXME, implement this when
   * we are not using our own clock */
  *start = GST_CLOCK_TIME_NONE;
  *end = GST_CLOCK_TIME_NONE;
}

static gboolean
gst_base_audio_src_event (GstBaseSrc * bsrc, GstEvent * event)
{
  GstBaseAudioSrc *src = GST_BASE_AUDIO_SRC (bsrc);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      gst_ring_buffer_pause (src->ringbuffer);
      gst_ring_buffer_clear_all (src->ringbuffer);
      break;
    case GST_EVENT_FLUSH_STOP:
      /* always resync on sample after a flush */
      src->next_sample = -1;
      gst_ring_buffer_clear_all (src->ringbuffer);
      break;
    default:
      break;
  }
  return TRUE;
}

static GstFlowReturn
gst_base_audio_src_create (GstBaseSrc * bsrc, guint64 offset, guint length,
    GstBuffer ** outbuf)
{
  GstBaseAudioSrc *src = GST_BASE_AUDIO_SRC (bsrc);
  GstBuffer *buf;
  guchar *data;
  guint samples;
  guint res;
  guint64 sample;
  gint bps;
  GstRingBuffer *ringbuffer;

  ringbuffer = src->ringbuffer;

  if (G_UNLIKELY (!gst_ring_buffer_is_acquired (ringbuffer)))
    goto wrong_state;

  bps = ringbuffer->spec.bytes_per_sample;

  if ((length == 0 && bsrc->blocksize == 0) || length == -1)
    /* no length given, use the default segment size */
    length = ringbuffer->spec.segsize;
  else
    /* make sure we round down to an integral number of samples */
    length -= length % bps;

  /* calculate the sequentially next sample we need to read */
  sample = (src->next_sample != -1 ? src->next_sample : 0);

  if (G_UNLIKELY (offset != -1)) {
    /* if a specific offset was given it must be the next
     * sequential offset we expect or we fail. */
    if (offset / bps != sample)
      goto wrong_offset;
  }

  /* get the number of samples to read */
  samples = length / bps;

  /* FIXME, using a bufferpool would be nice here */
  buf = gst_buffer_new_and_alloc (length);
  data = GST_BUFFER_DATA (buf);

  res = gst_ring_buffer_read (ringbuffer, sample, data, samples);
  if (G_UNLIKELY (res == -1))
    goto stopped;

  /* FIXME, we timestamp against our own clock, also handle the case
   * where we are slaved to another clock. We currently refuse to accept
   * any other clock than the one we provide, so this code is fine for
   * now. */
  GST_BUFFER_TIMESTAMP (buf) = gst_util_uint64_scale_int (sample,
      GST_SECOND, ringbuffer->spec.rate);
  src->next_sample = sample + samples;
  GST_BUFFER_DURATION (buf) = gst_util_uint64_scale_int (src->next_sample,
      GST_SECOND, ringbuffer->spec.rate) - GST_BUFFER_TIMESTAMP (buf);

  gst_buffer_set_caps (buf, GST_PAD_CAPS (GST_BASE_SRC_PAD (bsrc)));

  *outbuf = buf;

  return GST_FLOW_OK;

  /* ERRORS */
wrong_state:
  {
    GST_DEBUG_OBJECT (src, "ringbuffer in wrong state");
    return GST_FLOW_WRONG_STATE;
  }
wrong_offset:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SEEK,
        (NULL), ("resource can only be operated on sequentially but offset %"
            G_GUINT64_FORMAT " was given", offset));
    return GST_FLOW_ERROR;
  }
stopped:
  {
    gst_buffer_unref (buf);
    GST_DEBUG_OBJECT (src, "ringbuffer stopped");
    return GST_FLOW_WRONG_STATE;
  }
}

GstRingBuffer *
gst_base_audio_src_create_ringbuffer (GstBaseAudioSrc * src)
{
  GstBaseAudioSrcClass *bclass;
  GstRingBuffer *buffer = NULL;

  bclass = GST_BASE_AUDIO_SRC_GET_CLASS (src);
  if (bclass->create_ringbuffer)
    buffer = bclass->create_ringbuffer (src);

  if (G_LIKELY (buffer))
    gst_object_set_parent (GST_OBJECT_CAST (buffer), GST_OBJECT_CAST (src));

  return buffer;
}

void
gst_base_audio_src_callback (GstRingBuffer * rbuf, guint8 * data, guint len,
    gpointer user_data)
{
  //GstBaseAudioSrc *src = GST_BASE_AUDIO_SRC (data);
}

static GstStateChangeReturn
gst_base_audio_src_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstBaseAudioSrc *src = GST_BASE_AUDIO_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (src->ringbuffer == NULL) {
        src->ringbuffer = gst_base_audio_src_create_ringbuffer (src);
        gst_ring_buffer_set_callback (src->ringbuffer,
            gst_base_audio_src_callback, src);
      }
      if (!gst_ring_buffer_open_device (src->ringbuffer))
        return GST_STATE_CHANGE_FAILURE;
      src->next_sample = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_ring_buffer_set_flushing (src->ringbuffer, FALSE);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      gst_ring_buffer_may_start (src->ringbuffer, TRUE);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      gst_ring_buffer_may_start (src->ringbuffer, FALSE);
      gst_ring_buffer_pause (src->ringbuffer);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_ring_buffer_set_flushing (src->ringbuffer, TRUE);
      gst_ring_buffer_release (src->ringbuffer);
      src->next_sample = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_ring_buffer_close_device (src->ringbuffer);
      gst_object_unparent (GST_OBJECT_CAST (src->ringbuffer));
      src->ringbuffer = NULL;
      break;
    default:
      break;
  }

  return ret;
}
