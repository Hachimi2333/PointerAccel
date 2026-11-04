#include "maintenance/installation.hpp"
#include <QCoreApplication>
#include <QTextStream>
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	QString error;
	if (pa::verifyInstallation(&error))
		return 0;
	QTextStream(stderr) << error << '\n';
	return 1;
}
