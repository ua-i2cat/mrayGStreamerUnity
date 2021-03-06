

#include "stdafx.h"
#include "GstPipelineHandler.h"
#include "UnityHelpers.h"
#include "GStreamerCore.h"

#include <gst/net/gstnet.h>

namespace mray
{
namespace video
{

	class GstPipelineHandlerImpl
	{
	public:
		GstPipelineHandlerImpl()
		{
			gstPipeline = 0;
			closing = false;
			paused = true;
			Loaded = false;
			playing = false;

			baseTime = 0;
			isMasterClock = false;
			clockProvider = 0;

			clock = 0;
			clockPort = 0;
		}

		guint busWatchID;
		GstElement* gstPipeline;
		GstNetTimeProvider* clockProvider;
		GstClock* clock;

		bool  paused;
		bool  Loaded;
		bool  playing;
		bool  closing;

		uint clockPort;
		std::string clockIP;
		ulong baseTime;
		bool isMasterClock;
	};

	GstPipelineHandler::GstPipelineHandler()
	{
		GStreamerCore::Instance()->Ref();

		m_data = new GstPipelineHandlerImpl();

	}
	GstPipelineHandler::~GstPipelineHandler()
	{
		Close();
		GStreamerCore::Instance()->Unref();
		delete m_data;
	}

	bool GstPipelineHandler::CreatePipeline(bool isMasterClock, const std::string& clockIP, uint clockPort)
	{
		if (!m_data->gstPipeline)
			return false;

		//enable logging to stdout
		g_signal_connect(m_data->gstPipeline, "deep-notify", G_CALLBACK(gst_object_default_deep_notify), NULL);


		m_data->clockIP = clockIP;
		m_data->clockPort = clockPort;
		m_data->isMasterClock = isMasterClock;
		m_data->paused = true;
		m_data->Loaded = false;
		m_data->playing = false;

		GstBus * bus = gst_pipeline_get_bus(GST_PIPELINE(m_data->gstPipeline));

		if (bus){
			m_data->busWatchID = gst_bus_add_watch(bus, (GstBusFunc)busFunction, this);
		}

		gst_object_unref(bus);



		if (gst_element_set_state(GST_ELEMENT(m_data->gstPipeline), GST_STATE_READY) == GST_STATE_CHANGE_FAILURE) {
			LogMessage("GStreamerNetworkPlayer::Play(): unable to set pipeline to ready",ELL_WARNING);
			return false;
		}
		if (gst_element_get_state(GST_ELEMENT(m_data->gstPipeline), NULL, NULL, 10 * GST_SECOND) == GST_STATE_CHANGE_FAILURE){
			LogMessage("GStreamerNetworkPlayer::Play(): unable to get pipeline ready status", ELL_WARNING);
			return false;
		}

		// pause the pipeline
		if (gst_element_set_state(GST_ELEMENT(m_data->gstPipeline), GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
			LogMessage("GStreamerNetworkPlayer::Play(): unable to pause pipeline", ELL_WARNING);
			return false;
		}

		return true;
	}

	void GstPipelineHandler::SetPaused(bool p)
	{
		m_data->paused = p;
		if (m_data->Loaded)
		{
			if (m_data->playing)
			{
				if (m_data->paused)
					gst_element_set_state(m_data->gstPipeline, GST_STATE_PAUSED);
				else
					gst_element_set_state(m_data->gstPipeline, GST_STATE_PLAYING);
			}
			else
			{
				GstState state = GST_STATE_PAUSED;
				gst_element_set_state(m_data->gstPipeline, state);
				gst_element_get_state(m_data->gstPipeline, &state, NULL, 2 * GST_SECOND);
				if (!m_data->paused)
					gst_element_set_state(m_data->gstPipeline, GST_STATE_PLAYING);
				m_data->playing = true;
			}
		}
	}
	void GstPipelineHandler::Stop()
	{
		if (!m_data->Loaded)return;
		GstState state;
		if (!m_data->paused){
			state = GST_STATE_PAUSED;
			gst_element_set_state(m_data->gstPipeline, state);
			gst_element_get_state(m_data->gstPipeline, &state, NULL, 2 * GST_SECOND);
		}

		state = GST_STATE_READY;
		gst_element_set_state(m_data->gstPipeline, state);
		gst_element_get_state(m_data->gstPipeline, &state, NULL, 2 * GST_SECOND);
		m_data->playing = false;
		m_data->paused = true;
	}
	bool GstPipelineHandler::IsLoaded()
	{
		return m_data->Loaded;
	}
	bool GstPipelineHandler::IsPlaying()
	{
		return m_data->playing;
	}
	bool GstPipelineHandler::QueryLatency(bool &isLive, ulong& minLatency, ulong& maxLatency)
	{
		bool ok = false;
		GstQuery * q = gst_query_new_latency();
		if (gst_element_query(m_data->gstPipeline, q)) {
			GstClockTime minlat = 0, maxlat = 0;
			gboolean live;
			gst_query_parse_latency(q, &live, &minlat, &maxlat);
			isLive = live;
			minLatency = minlat;
			maxLatency = maxlat;
			ok=true;
		}
		gst_query_unref(q);
		return ok;
	}
	void GstPipelineHandler::Close()
	{

		Stop();

		if (m_data->Loaded){
			gst_element_set_state(GST_ELEMENT(m_data->gstPipeline), GST_STATE_NULL);
			gst_element_get_state(m_data->gstPipeline, NULL, NULL, 2 * GST_SECOND);

			if (m_data->busWatchID != 0) g_source_remove(m_data->busWatchID);

			gst_object_unref(m_data->gstPipeline);
			m_data->gstPipeline = NULL;
		}

		m_data->Loaded = false;
	}
	bool GstPipelineHandler::HandleMessage(GstBus * bus, GstMessage * msg)
	{
		switch (GST_MESSAGE_TYPE(msg)) {

		case GST_MESSAGE_BUFFERING:
			gint pctBuffered;
			gst_message_parse_buffering(msg, &pctBuffered);
			break;

#if GST_VERSION_MAJOR==0
		case GST_MESSAGE_DURATION:{
			GstFormat format = GST_FORMAT_TIME;
			gst_element_query_duration(m_data->gstPipeline, &format, &durationNanos);
		}break;
#else
		case GST_MESSAGE_DURATION_CHANGED:
			//gst_element_query_duration(m_data->gstPipeline, GST_FORMAT_TIME, &durationNanos);
			break;

#endif

		case GST_MESSAGE_STATE_CHANGED:{
			GstState oldstate, newstate, pendstate;
			gst_message_parse_state_changed(msg, &oldstate, &newstate, &pendstate);
			if (newstate == GST_STATE_PAUSED && !m_data->playing){
				m_data->Loaded = true;
				m_data->playing = true;
				if (!m_data->paused){
					SetPaused(false);
				}
			}
			else if (newstate == GST_STATE_READY)
			{
				FIRE_LISTENR_METHOD(OnPipelineReady, (this));
			}
			else if (newstate == GST_STATE_PLAYING)
			{
				FIRE_LISTENR_METHOD(OnPipelinePlaying, (this));
			}
			else if (newstate == GST_STATE_PAUSED)
			{
				FIRE_LISTENR_METHOD(OnPipelineStopped, (this));
			}

		}break;

		case GST_MESSAGE_ASYNC_DONE:
			break;

		case GST_MESSAGE_WARNING:
		{
			GError *err;
			gchar *debug;
			gst_message_parse_warning(msg, &err, &debug);
			gchar * name = gst_element_get_name(GST_MESSAGE_SRC(msg));

			LogMessage(std::string("GStreamerNetworkPlayer::HandleMessage(): warning in module ") + name + std::string("  reported: ") + err->message, ELL_WARNING);

			g_free(name);
			g_error_free(err);
			g_free(debug);

		//	gst_element_set_state(GST_ELEMENT(m_data->gstPipeline), GST_STATE_NULL);

		}break;
		case GST_MESSAGE_ERROR:
		{
			GError *err;
			gchar *debug;
			gst_message_parse_error(msg, &err, &debug);
			gchar * name = gst_element_get_name(GST_MESSAGE_SRC(msg));

			LogMessage(std::string("GStreamerNetworkPlayer::HandleMessage(): error in module ")
				+ name + std::string("  reported: ") + err->message + std::string(" - ") + debug, ELL_WARNING);

			g_free(name);
			g_error_free(err);
			g_free(debug);

			FIRE_LISTENR_METHOD(OnPipelineError, (this));

			gst_element_set_state(GST_ELEMENT(m_data->gstPipeline), GST_STATE_NULL);

		}break;


		default:
			break;
		}

		return true;
	}


	void GstPipelineHandler::SetClockBaseTime(ulong baseTime)
	{
		m_data->baseTime = baseTime;
		if (m_data->gstPipeline)
		{
			GstClock* clock = gst_pipeline_get_clock(GST_PIPELINE(m_data->gstPipeline));

			gst_element_set_base_time(m_data->gstPipeline, baseTime);// -gst_clock_get_time(clock));
		}
	}
	ulong GstPipelineHandler::GetClockBaseTime()
	{
		if (m_data->clock)
			return gst_clock_get_time(m_data->clock);
		return m_data->baseTime;
	}
	bool GstPipelineHandler::busFunction(GstBus * bus, GstMessage * message, GstPipelineHandler * player)
	{
		return player->HandleMessage(bus, message);

	}
	void GstPipelineHandler::SetPipeline(GstElement* p)
	{
		m_data->gstPipeline = p;
	}
	GstElement* GstPipelineHandler::GetPipeline()
	{
		return m_data->gstPipeline;
	}




}
}

