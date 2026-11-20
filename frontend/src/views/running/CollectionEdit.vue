<template>
  <div class="page-card">
    <div class="page-header">
      <h3>采集项编辑 #{{ collectionId }}</h3>
      <el-button @click="$router.back()">返回</el-button>
    </div>
    <el-form :model="form" label-width="120px" style="max-width: 500px">
      <el-form-item label="采集项编码">
        <DictSelect v-model="form.itemCode" dict-key="dataItems" style="width: 100%"/>
      </el-form-item>
      <el-form-item label="采集周期(ms)">
        <el-input-number v-model="form.collectionCycle" :min="100" :step="100"/>
      </el-form-item>
      <el-form-item label="地址覆盖">
        <el-input v-model="form.addressOverride" placeholder="留空使用默认地址"/>
      </el-form-item>
      <el-form-item label="启用">
        <el-switch v-model="form.enabled"/>
      </el-form-item>
      <el-form-item>
        <el-button type="primary">保存</el-button>
      </el-form-item>
    </el-form>
  </div>
</template>

<script lang="ts" setup>
import {computed, reactive} from 'vue'
import {useRoute} from 'vue-router'
import DictSelect from '@/components/DictSelect.vue'

const route = useRoute()
const collectionId = computed(() => Number(route.params.id))

const form = reactive({
  itemCode: '',
  collectionCycle: 1000,
  addressOverride: '',
  enabled: true,
})
</script>