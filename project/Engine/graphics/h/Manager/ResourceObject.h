/**
 * @file ResourceObject.h
 * @brief DirectX12�̃��\�[�X�iID3D12Resource�j�̎�����Ǘ����A����Y���h�����߂̃��b�p�[�N���X
 */
#pragma once
#include <d3d12.h>
namespace engine::graphics {
/**
 * @brief COM�I�u�W�F�N�g�iID3D12Resource�j�̉�����������������RAII�N���X
 * @note ComPtr�̊ȈՔł̂悤�Ȗ����ʂ����ϐ��̃X�R�[�v�𔲂����ہi�������s�����ہj��
 * ������ Release() ���Ă΂�邽�߁A���������[�N����S�ɖh�����Ƃ��ł���͂�
 */
class ResourceObject {
public:

    /**
     * @brief �R���X�g���N�^�B�Ǘ��ΏۂƂȂ�DirectX12���\�[�X�̃|�C���^��󂯎��
     * @param resource �Ǘ��ΏۂƂ��� ID3D12Resource �̐��|�C���^
     */
    ResourceObject(ID3D12Resource* resource)
        : resource_(resource)
    {
    }

    /**
     * @brief �f�X�g���N�^�B�ێ����Ă��郊�\�[�X���L���ł���΁A�����I�ɉ���iRelease�j����
     */
    ~ResourceObject()
    {
        if (resource_) {
            resource_->Release();
        }
    }

    /**
     * @brief �Ǘ����Ă��郊�\�[�X�̐��|�C���^��擾����
     * @return ID3D12Resource* DirectX12���\�[�X�̐��|�C���^
     * @note DirectX��API�i�e��r���[�̍쐬��R�}���h�ւ̃Z�b�g�Ȃǁj�ɒ��ڃ|�C���^��n���ۂɎg�p
     */
    ID3D12Resource* Get() { return resource_; }

private:

    /** @brief �Ǘ��ΏۂƂȂ��Ă���DirectX12���\�[�X�̃|�C���^ */
    ID3D12Resource* resource_;
};

} // namespace engine::graphics
