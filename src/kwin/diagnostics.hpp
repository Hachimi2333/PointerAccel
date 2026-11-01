#pragma once
#include "core/pointer_engine.hpp"
#include <QElapsedTimer>
#include <array>
#include <QJsonObject>
namespace pa
{
struct Sample
{
	QString device;
	double rawX = 0, rawY = 0, scale = 1;
	pa::Motion output;
	std::uint16_t dpi = 96;
	std::uint64_t time = 0;
};

class InputDiagnostics
{
public:
	InputDiagnostics();
	void record(const Sample& sample, bool tracing);
	void clearTrace();
	QJsonObject snapshot(bool tracing, const QString& fault) const;
	std::uint64_t processed = 0, passthrough = 0, batches = 0;

private:
	QElapsedTimer clock;
	qint64 lastReport = -1;
	Sample last;
	static constexpr int traceCapacity = 4096;
	std::array<Sample, traceCapacity> trace;
	int traceNext = 0, traceCount = 0;
};
}
