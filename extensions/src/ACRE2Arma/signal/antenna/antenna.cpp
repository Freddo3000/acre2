#include "antenna.hpp"

#include <algorithm>
#include <glm/geometric.hpp>
#include <glm/gtx/intersect.hpp>
#include <glm/gtx/normal.hpp>

acre::signal::antenna::antenna(std::istream & stream_, const AntennaPolarization polarization_, const float32_t internalLoss_dBm_ )
: _polarization(polarization_), _internalLos_dBm(internalLoss_dBm_){
    stream_.read((char *)&_min_frequency, sizeof(float32_t ));
    stream_.read((char *)&_max_frequency, sizeof(float32_t ));
    stream_.read((char *)&_frequency_step, sizeof(float32_t ));

    stream_.read((char *)&_total_entries, sizeof(uint32_t));
    stream_.read((char *)&_width, sizeof(uint32_t));
    stream_.read((char *)&_height, sizeof(uint32_t));

    stream_.read((char *)&_elevation_step, sizeof(uint32_t));
    stream_.read((char *)&_direction_step, sizeof(uint32_t));

    const uint32_t map_size = _width * _height * _total_entries;

    _gain_map = new antenna_gain_entry[map_size];

    stream_.read((char *)_gain_map, sizeof(antenna_gain_entry)*map_size);
}

float32_t acre::signal::antenna::gain(const glm::vec3 dir_antenna_, const glm::vec3 dir_signal_, const float32_t f_)
{
    if ((f_ < _min_frequency) || ((f_ + _frequency_step) > _max_frequency)){
        return -1000.0f;
    }
    if (glm::length(dir_signal_) <= 0.0f) {
        return 0.0f; // cannot get direction if antennas have same position
    }

    const glm::vec3 dir_antenna_v = glm::normalize(dir_antenna_);
    const float32_t elev_antenna = asinf(dir_antenna_v.z)*57.2957795f;
    const float32_t dir_antenna = atan2f(dir_antenna_v.x, dir_antenna_v.y)*57.2957795f;

    const glm::vec3 dir_signal_v = glm::normalize(dir_signal_);
    const float32_t elev_signal = asinf(dir_signal_v.z)*57.2957795f;
    const float32_t dir_signal = atan2f(dir_signal_v.x, dir_signal_v.y)*57.2957795f;

    float32_t dir = fmodf(dir_antenna + dir_signal, 360.0f);
    float32_t elev = elev_antenna + elev_signal;

    if (elev > 90.0f || elev < -90.0f) {
        dir = fmodf(dir + 180.0f, 360.0f);
        if (elev < -90.0f) {
            elev = -90.0f + (std::fabs(elev) - 90.0f);
        }
        else {
            elev = 90.0f - (elev - 90.0f);
        }
    }

    if (dir < 0.0f) {
        dir = dir + 360.0f;
    }

    elev = 90.0f - std::fabs(elev);

    const float32_t lower_freq_gain = _get_gain(f_, dir, elev);
    const float32_t upper_freq_gain = _get_gain(f_ + _frequency_step, dir, elev);

    const float32_t lower_freq = f_ - fmodf(f_, _frequency_step);
    const float32_t upper_freq = lower_freq + _frequency_step;

    const float32_t total_gain = _interp(f_, lower_freq, upper_freq, lower_freq_gain, upper_freq_gain);

    return total_gain;
}

acre::signal::AntennaPolarization acre::signal::antenna::getPolarization() {
    return this->_polarization;
}

void acre::signal::antenna::setPolarization(const AntennaPolarization polarization_) {
    this->_polarization = polarization_;
}

float32_t acre::signal::antenna::getInternalLoss_dBm() {
    return this->_internalLos_dBm;
}
void acre::signal::antenna::setInternalLoss_dBm(const float32_t internalLos_dBm_) {
    this->_internalLos_dBm = internalLos_dBm_;
}

float32_t acre::signal::antenna::_get_gain(const float32_t f_, const float32_t dir_, const float32_t elev_)
{
    uint32_t f_index = (uint32_t)std::floor((f_ - _min_frequency)/_frequency_step);
    if (f_index > _total_entries - 1u){
        f_index = _total_entries - 1u;
    }
    const uint32_t dir_index_min = (uint32_t)std::floor(dir_ / _direction_step);
    uint32_t dir_index_max = (uint32_t)std::floor((dir_ + _direction_step) / _direction_step);

    if (dir_index_max > _height - 1u) {
        dir_index_max = 0u;
    }

    const uint32_t elev_index_min = (uint32_t)std::floor(elev_ / _elevation_step);
    uint32_t elev_index_max = (uint32_t)std::floor((elev_ + _elevation_step) / _elevation_step);
    if (elev_index_max > _width - 1u) {
        elev_index_max = _width - 1u;
    }

    const float32_t gain_d_min_e_min = _gain_map[f_index * (_width * _height) + (dir_index_min * _width) + elev_index_min].v;
    const float32_t gain_d_min_e_max = _gain_map[f_index * (_width * _height) + (dir_index_min * _width) + elev_index_max].v;

    const float32_t gain_d_max_e_min = _gain_map[f_index * (_width * _height) + (dir_index_max * _width) + elev_index_min].v;
    const float32_t gain_d_max_e_max = _gain_map[f_index * (_width * _height) + (dir_index_max * _width) + elev_index_max].v;

    const float32_t dir_min = dir_ - fmodf(dir_, _direction_step);
    const float32_t dir_max = dir_min + _direction_step;

    const float32_t elev_min = elev_ - fmodf(elev_, _elevation_step);
    const float32_t elev_max = elev_min + _elevation_step;

    const float32_t elev_lower_gain = _interp(dir_, dir_min, dir_max, gain_d_min_e_min, gain_d_max_e_min);
    const float32_t elev_upper_gain = _interp(dir_, dir_min, dir_max, gain_d_min_e_max, gain_d_max_e_max);

    const float32_t total_gain = _interp(elev_, elev_min, elev_max, elev_lower_gain, elev_upper_gain);

    return total_gain;
}

float32_t acre::signal::antenna::_interp(const float32_t g_, const float32_t g1_, const float32_t g2_, const float32_t d1_, const float32_t d2_) {
    return d1_ + (std::max((g_ - g1_), 0.00001f) / std::max((g2_ - g1_), 0.00001f))*(d2_ - d1_);
}
