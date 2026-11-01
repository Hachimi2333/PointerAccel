#include "diagnostics.hpp"
#include <QJsonArray>
#include <algorithm>
namespace pa
{
namespace
{
QJsonObject sampleJson(const Sample& s)
{
	return {{"device", s.device},
	    {"dpi", s.dpi},
	    {"scale", s.scale},
	    {"raw", QJsonArray{s.rawX, s.rawY}},
	    {"output", QJsonArray{s.output.x, s.output.y}},
	    {"time_usec", static_cast<double>(s.time)}};
}

}
InputDiagnostics::InputDiagnostics()
{
	clock.start();
}
void InputDiagnostics::clearTrace()
{
	traceCount = 0;
	traceNext = 0;
}
void InputDiagnostics::record(const Sample& sample, bool tracing)
{
	++processed;
	lastReport = clock.elapsed();
	last = sample;
	if (tracing)
	{
		trace[traceNext] = last;
		traceNext = (traceNext + 1) % traceCapacity;
		traceCount = std::min(traceCount + 1, traceCapacity);
	}
}
QJsonObject InputDiagnostics::snapshot(bool tracing, const QString& fault) const
{
	QJsonObject result{{"processed", static_cast<double>(processed)},
	    {"passthrough", static_cast<double>(passthrough)},
	    {"batches", static_cast<double>(batches)},
	    {"last_age_ms", lastReport < 0 ? -1 : (clock.elapsed() - lastReport)},
	    {"last", lastReport < 0 ? QJsonObject() : sampleJson(last)},
	    {"error", fault},
	    {"trace_enabled", tracing},
	    {"trace_samples", traceCount},
	    {"trace_capacity", traceCapacity}};
	if (tracing)
	{
		QJsonArray samples;
		for (int i = 0; i < traceCount; ++i)
			samples.append(sampleJson(trace[(traceNext + traceCapacity - traceCount + i) % traceCapacity]));
		result["trace"] = samples;
	}
	return result;
}
}
