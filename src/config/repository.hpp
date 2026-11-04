#pragma once
#include "config.hpp"
#include <QLockFile>
#include <functional>
namespace pa
{
struct SaveResult
{
	bool saved = false;
	bool conflict = false;
	QString error;
	ConfigSnapshot snapshot;
};

// The transaction remains alive until the caller has requested runtime application.
// That preserves the original writer lock across ApplyConfig and its confirmation.
class ConfigRepository
{
public:
	explicit ConfigRepository(QString path);
	ConfigSnapshot snapshot() const;
	bool write(const Config& config, QString* error) const;
	class Transaction;

private:
	QString path;
};
class ConfigRepository::Transaction
{
public:
	explicit Transaction(const ConfigRepository& repository);
	SaveResult save(const std::function<void(Config&)>& edit, const QString* expectedRevision = nullptr);

private:
	const ConfigRepository& repository;
	QLockFile lock;
	QString error;
};
}
