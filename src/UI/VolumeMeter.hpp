#pragma once

#include <QWidget>

namespace Acheron {
namespace UI {

// The green-to-red input level bar Discord shows next to the microphone test.
class VolumeMeter : public QWidget
{
    Q_OBJECT
public:
    explicit VolumeMeter(QWidget *parent = nullptr);

    void setLevel(float rms);
    void setThreshold(float threshold);

    QSize sizeHint() const override { return QSize(100, 12); }
    QSize minimumSizeHint() const override { return QSize(40, 12); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    float level = 0.0f;
    float threshold = -1.0f;

    static constexpr float MAX_RMS = 2000.0f;
};

} // namespace UI
} // namespace Acheron
